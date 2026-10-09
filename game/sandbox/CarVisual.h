#pragma once
#include "WickedEngine.h"

#include "racer/Customization.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// Shared by the race and the garage preview (D-055): how a loaded car or track model is made to
// look like the game - toon materials, the chosen customisation parts, the paint.

// Wicked's cartoon shader and ink outlines on every material (D-050); ground and corrugated
// sheet materials draw no outline (D-050, D-054).
void ToonifyMaterials(wi::scene::Scene& s);

// Slot -> variant for a car from its catalog file (content/customization/<id>.json); invalid or
// missing choices fall back to stock. Empty when the car has no catalog (local cars).
std::map<std::string, std::string> ResolveCarParts(const std::string& catalog_file, const std::map<std::string, std::string>& choice);

// Removes every PART_<slot>_<variant> and RIM_<variant>_<wheel> not in `resolved`. No-op when
// `resolved` is empty, so a model without variants is left alone.
void KeepCarParts(wi::scene::Scene& s, const std::map<std::string, std::string>& resolved);

// Base colour of every material named "*paint*" (the livery's accents keep their colour).
void PaintCar(wi::scene::Scene& s, const XMFLOAT4& paint);

// Sits the driver character (cooked characters/driver.wiscene, D-076) in a loaded car: every pivot
// moves under the car's BODY, shifted from the character's hip to the car's DRIVER_ANCHOR. The look
// picks one of the faces (it becomes DRIVER_HEAD) and recolours skin, hair and jacket; the gesture
// hands start hidden. Appends the driver's materials to `mats`. False if the car has no anchor.
bool AttachDriver(wi::scene::Scene& s, wi::ecs::Entity car_root, const std::string& file, const racer::DriverLook& look,
                  std::vector<std::pair<wi::ecs::Entity, std::string>>* mats = nullptr);
