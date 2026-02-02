# Critical Bugs - Status Update

## Bug 1: Mom Dialog Not Triggering After First Line
**Status:** FIXED
**Fix:** Simplified canTalk logic to `canTalk = (currentDay < 5)` - player can always talk to Mom in restaurant on days 1-4
**Location:** `src/game/main.c` line ~1727

## Bug 2: Enforcer Spawn Issues
**Status:** FIXED
**Fix:** Changed from random house-based spawning to fixed street positions:
- Enforcer 0: (7168, 8192) - between upper houses
- Enforcer 1: (6144, 3072) - lower part of upper area
- Enforcer 2: (-512, 3072) - center of horizontal corridor
- Enforcer 3: (-6656, 2048) - left side of corridor
- Enforcer 4: (-10240, 0) - deep left of corridor
**Location:** `src/game/main.c` lines 865-895

## Bug 3: Mask Not Rendering in Exterior
**Status:** INVESTIGATING
**Code is in place:** Lines 2548-2553 check `else if (hasMask)` and call `drawCharacterItem` with mask model
**Possible issues:**
- `hasMask` variable not being set to true (check line 2103 where food delivery sets it)
- `hasFood` is still true (blocking the else-if)
- Mask model issue

---

## Build Process
Fixed CMake build-web target to use mkpsxiso properly:
```
cmake --build build                      # Compile code
cmake --build build --target build-web   # Create ISO with CD audio
```
ROM files are at: `web/rom/lander.rom` (~36MB)
