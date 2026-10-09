"""LLM-only coach (P7 baseline): what most "AI coach" entries do - give the model the race stats
and ask for advice. Its advice is then tested by re-simulation like everyone else's.

Credentials (pick one):
  Azure OpenAI (works on Azure for Students): PROOFPLAY_AOAI_ENDPOINT (https://<name>.openai.azure.com)
                     + PROOFPLAY_AOAI_KEY + PROOFPLAY_AOAI_DEPLOYMENT  - stdlib HTTP, no package needed
                     (optional PROOFPLAY_AOAI_API_VERSION, default 2025-04-01-preview)
  Microsoft Foundry (Claude; needs a pay-as-you-go subscription): PROOFPLAY_FOUNDRY_KEY + PROOFPLAY_FOUNDRY_RESOURCE
  Claude API:        ANTHROPIC_API_KEY
Model for Claude: PROOFPLAY_LLM_MODEL (default claude-opus-5-5; on Foundry use your deployment name).
"""
import importlib.util
import json
import os
import urllib.error
import urllib.parse
import urllib.request
import pathlib

# Optional local credentials file (never committed): proofplay/llm.env with lines KEY=VALUE.
# Variables already set in the terminal win over the file.
_ENV_FILE = pathlib.Path(__file__).resolve().parents[1] / "llm.env"
if _ENV_FILE.exists():
    for _line in _ENV_FILE.read_text(encoding="utf-8").splitlines():
        _k, _sep, _v = _line.strip().partition("=")
        if _sep and _k and not _k.startswith("#"):
            os.environ.setdefault(_k.strip(), _v.strip().strip('"'))

SYSTEM = """You are a racing coach for a combat racing game. You get one race's corner statistics
(time lost against the ideal per pass, entry/min/exit speed in m/s, where braking began before
the apex). Pick the three passes where a change of braking point would gain the most time and say
whether the driver should brake earlier or later, and by how many metres (5 to 30)."""

SCHEMA = {
    "type": "object",
    "properties": {
        "advice": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "corner": {"type": "integer"},
                    "lap": {"type": "integer"},
                    "kind": {"type": "string", "enum": ["brake_earlier", "brake_later"]},
                    "amount_m": {"type": "number"},
                },
                "required": ["corner", "lap", "kind", "amount_m"],
                "additionalProperties": False,
            },
        }
    },
    "required": ["advice"],
    "additionalProperties": False,
}


def azure_openai():
    return all(os.getenv(k) for k in ("PROOFPLAY_AOAI_ENDPOINT", "PROOFPLAY_AOAI_KEY", "PROOFPLAY_AOAI_DEPLOYMENT"))


def available():
    if azure_openai():
        return True
    if importlib.util.find_spec("anthropic") is None:
        return False
    return bool((os.getenv("PROOFPLAY_FOUNDRY_KEY") and os.getenv("PROOFPLAY_FOUNDRY_RESOURCE")) or os.getenv("ANTHROPIC_API_KEY"))


def client():
    import anthropic  # only needed when the LLM coach runs

    if os.getenv("PROOFPLAY_FOUNDRY_KEY"):
        return anthropic.AnthropicFoundry(api_key=os.environ["PROOFPLAY_FOUNDRY_KEY"], resource=os.environ["PROOFPLAY_FOUNDRY_RESOURCE"])
    return anthropic.Anthropic()


def stats_for(report):
    """Only the numbers a stats-based coach would see - no simulation."""
    return {
        "track": report["setup"]["track"],
        "corners": [
            {"corner": c["id"], "ideal_apex_speed": round(c["ideal_speed"], 1),
             "passes": [{k: (round(v, 2) if isinstance(v, float) else v) for k, v in p.items()} for p in c["passes"]]}
            for c in report["corners"]
        ],
    }


def advise_azure_openai(report, usage_log):
    # Key auth needs the resource endpoint. A Foundry project URL (.../api/projects/<name>) or a
    # pasted full request URL is cut back to https://<host>.
    parts = urllib.parse.urlsplit(os.environ["PROOFPLAY_AOAI_ENDPOINT"].strip())
    url = (f"{parts.scheme}://{parts.netloc}" + "/openai/deployments/"
           + os.environ["PROOFPLAY_AOAI_DEPLOYMENT"] + "/chat/completions?api-version=" + os.getenv("PROOFPLAY_AOAI_API_VERSION", "2025-04-01-preview"))
    body = {
        "messages": [{"role": "system", "content": SYSTEM}, {"role": "user", "content": json.dumps(stats_for(report))}],
        "response_format": {"type": "json_schema", "json_schema": {"name": "advice", "schema": SCHEMA, "strict": True}},
        "max_completion_tokens": 16000,  # reasoning models (gpt-5-mini) spend part of this thinking
    }
    req = urllib.request.Request(url, data=json.dumps(body).encode("utf-8"), method="POST",
                                 headers={"Content-Type": "application/json", "api-key": os.environ["PROOFPLAY_AOAI_KEY"].strip().strip("\"'").strip()})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            data = json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        # Azure explains auth/deployment/quota problems in the body; the key is never printed.
        detail = e.read().decode("utf-8", "replace")[:600]
        raise RuntimeError(f"Azure OpenAI HTTP {e.code} at {url.split('?')[0]}: {detail}") from None
    if usage_log is not None and "usage" in data:
        usage_log.append({"input_tokens": data["usage"].get("prompt_tokens"), "output_tokens": data["usage"].get("completion_tokens")})
    return json.loads(data["choices"][0]["message"]["content"])["advice"][:3]


def advise(report, usage_log=None):
    if azure_openai():
        return advise_azure_openai(report, usage_log)
    if not available():
        missing = [k for k in ("PROOFPLAY_AOAI_ENDPOINT", "PROOFPLAY_AOAI_KEY", "PROOFPLAY_AOAI_DEPLOYMENT") if not os.getenv(k)]
        raise RuntimeError("LLM coach not configured: set " + ", ".join(missing) + " in this terminal (Azure OpenAI)")
    c = client()
    response = c.messages.create(
        model=os.getenv("PROOFPLAY_LLM_MODEL", "claude-opus-5-5"),
        max_tokens=16000,
        system=SYSTEM,
        output_config={"effort": "medium", "format": {"type": "json_schema", "schema": SCHEMA}},
        messages=[{"role": "user", "content": json.dumps(stats_for(report))}],
    )
    if response.stop_reason == "refusal":
        return []
    if usage_log is not None:
        usage_log.append({"input_tokens": response.usage.input_tokens, "output_tokens": response.usage.output_tokens})
    text = next(b.text for b in response.content if b.type == "text")
    return json.loads(text)["advice"][:3]
