#include "config_types.h"

namespace config {

const char* actionTypeToString(ActionType type) {
  switch (type) {
    case ActionType::Combo:
      return "combo";
    case ActionType::Text:
      return "text";
    case ActionType::Media:
      return "media";
    case ActionType::Mouse:
      return "mouse";
    case ActionType::LayerSwitch:
      return "layer_switch";
    case ActionType::Sequence:
      return "sequence";
    case ActionType::None:
    
    default:
      return "none";
  }
}

ActionType actionTypeFromString(const String& value) {
  if (value == "combo") {
    return ActionType::Combo;
  }
  if (value == "text") {
    return ActionType::Text;
  }
  if (value == "media") {
    return ActionType::Media;
  }
  if (value == "mouse") {
    return ActionType::Mouse;
  }
  if (value == "layer_switch") {
    return ActionType::LayerSwitch;
  }
  if (value == "sequence") {
    return ActionType::Sequence;
  }
  return ActionType::None;

}

}  // namespace config
