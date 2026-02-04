cmake_minimum_required(VERSION 3.25)

# Reference the ps1-bare-metal SDK (local clone in sdk/ folder)
set(SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/sdk")
set(CMAKE_TOOLCHAIN_FILE "${SDK_PATH}/cmake/toolchain.cmake")

project(
	lander-baremetal
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "PSX Lander Model Viewer - Bare Metal"
)

# Set up compiler flags and tools
include(${SDK_PATH}/cmake/setup.cmake)
include(${SDK_PATH}/cmake/tools.cmake)

# Set path to local psxavenc
set(PSXAVENC_PATH "${CMAKE_CURRENT_LIST_DIR}/tools/psxavenc/psxavenc.exe" CACHE FILEPATH "Path to psxavenc" FORCE)

# Build common library from SDK
add_library(
	common OBJECT
	${SDK_PATH}/src/libc/clz.s
	${SDK_PATH}/src/libc/crt0.c
	${SDK_PATH}/src/libc/cxxsupport.cpp
	${SDK_PATH}/src/libc/malloc.c
	${SDK_PATH}/src/libc/misc.c
	${SDK_PATH}/src/libc/setjmp.s
	${SDK_PATH}/src/libc/string.c
	${SDK_PATH}/src/libc/string.s
	${SDK_PATH}/src/ps1/cache.s
	${SDK_PATH}/src/vendor/printf.c
)
target_include_directories(
	common PUBLIC
	${SDK_PATH}/src
	${SDK_PATH}/src/libc
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/engine
	${CMAKE_CURRENT_SOURCE_DIR}/src/game
)
link_libraries(common)

# Convert texture from PNG to raw 16bpp data
convertImage(
	assets/lander_ship.png 16
	lander/textureData.dat
)

# Convert font from PNG to raw 4bpp indexed data (texture + palette)
convertImage(
	assets/font.png 4
	lander/fontTexture.dat
	lander/fontPalette.dat
)

# Convert grass texture from PNG to raw 16bpp data (32x32)
convertImage(
	assets/grass.png 16
	lander/grassTexture.dat
)

# Convert title screen image from PNG to raw 16bpp data (256x256)
convertImage(
	assets/cool.png 16
	lander/titleTexture.dat
)

# Convert sound effect WAV to SPU-ADPCM format for embedding (bite sound)
# Using 22050 Hz mono for better quality on short sound effects
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/musicData.spu"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/bite1.wav"
	COMMAND
		"${PSXAVENC_PATH}"
		"-t" "spu"
		"-f" "22050"
		"${PROJECT_SOURCE_DIR}/assets/bite1.wav"
		"${PROJECT_BINARY_DIR}/lander/musicData.spu"
	VERBATIM
)

# Convert footstep sound effects to SPU-ADPCM format
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/sfx_step_grass.spu"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/sfx/03_Step_grass_03.wav"
	COMMAND
		"${PSXAVENC_PATH}"
		"-t" "spu"
		"-f" "22050"
		"${PROJECT_SOURCE_DIR}/assets/sfx/03_Step_grass_03.wav"
		"${PROJECT_BINARY_DIR}/lander/sfx_step_grass.spu"
	VERBATIM
)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/sfx_step_wood.spu"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/sfx/12_Step_wood_03.wav"
	COMMAND
		"${PSXAVENC_PATH}"
		"-t" "spu"
		"-f" "22050"
		"${PROJECT_SOURCE_DIR}/assets/sfx/12_Step_wood_03.wav"
		"${PROJECT_BINARY_DIR}/lander/sfx_step_wood.spu"
	VERBATIM
)

# Convert intro music WAV to SPU-ADPCM format (trim to 20 seconds first)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/introMusic_trimmed.wav"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Music/Intro_V3.wav"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/trimAudio.py"
		"${PROJECT_SOURCE_DIR}/assets/Music/Intro_V3.wav"
		"${PROJECT_BINARY_DIR}/lander/introMusic_trimmed.wav"
		"20"
	VERBATIM
)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/introMusic.spu"
	DEPENDS "${PROJECT_BINARY_DIR}/lander/introMusic_trimmed.wav"
	COMMAND
		"${PSXAVENC_PATH}"
		"-t" "spu"
		"-f" "11025"
		"-L"
		"${PROJECT_BINARY_DIR}/lander/introMusic_trimmed.wav"
		"${PROJECT_BINARY_DIR}/lander/introMusic.spu"
	VERBATIM
)

# Convert gameplay loop music WAV to SPU-ADPCM format (trim to 15 seconds, with loop)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/gameplayMusic_trimmed.wav"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Music/Gameplay_Loop.wav"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/trimAudio.py"
		"${PROJECT_SOURCE_DIR}/assets/Music/Gameplay_Loop.wav"
		"${PROJECT_BINARY_DIR}/lander/gameplayMusic_trimmed.wav"
		"15"
	VERBATIM
)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/gameplayMusic.spu"
	DEPENDS "${PROJECT_BINARY_DIR}/lander/gameplayMusic_trimmed.wav"
	COMMAND
		"${PSXAVENC_PATH}"
		"-t" "spu"
		"-f" "11025"
		"-l" "0"
		"${PROJECT_BINARY_DIR}/lander/gameplayMusic_trimmed.wav"
		"${PROJECT_BINARY_DIR}/lander/gameplayMusic.spu"
	VERBATIM
)

# Convert 3D model from OBJ to binary format (ship - kept for reference)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/modelData.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/ship_low_poly.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertModel.py"
		"${PROJECT_SOURCE_DIR}/assets/ship_low_poly.obj"
		"${PROJECT_BINARY_DIR}/lander/modelData.bin"
	VERBATIM
)

# Convert character body parts from OBJ to binary format
# Torso (kid_body_F_L) - depends on offsets header being generated first
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charBody.bin"
	DEPENDS
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_SOURCE_DIR}/src/game/kids_female_L_offsets.h"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charBody.bin"
		"-o" "kid_body_F_L"
		"-s" "20.0"
	VERBATIM
)

# Head (pivot at bottom = neck)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charHead.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charHead.bin"
		"-o" "kid_body_F_L_head"
		"-s" "20.0"
		"--pivot" "bottom"
	VERBATIM
)

# Left arm (pivot at top = shoulder)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charArmLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charArmLeft.bin"
		"-o" "kid_body_Arm_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Right arm (pivot at top = shoulder)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charArmRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charArmRight.bin"
		"-o" "kid_body_Arm_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Left leg/foot (pivot at top = hip)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charLegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charLegLeft.bin"
		"-o" "kid_body_Feet_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Right leg/foot (pivot at top = hip)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charLegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/charLegRight.bin"
		"-o" "kid_body_Feet_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Generate character offsets header
add_custom_command(
	OUTPUT "${PROJECT_SOURCE_DIR}/src/game/kids_female_L_offsets.h"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
		"--calculate-offsets"
		"-s" "20.0"
		"--offsets-output" "${PROJECT_SOURCE_DIR}/src/game/kids_female_L_offsets.h"
		"--char-name" "KIDS_FEMALE_L"
		"--part" "body=kid_body_F_L" "head=kid_body_F_L_head"
		"arm_left=kid_body_Arm_Left" "arm_right=kid_body_Arm_Right"
		"leg_left=kid_body_Feet_Left" "leg_right=kid_body_Feet_Right"
	VERBATIM
)

# Convert adult_female_L (Mom) character parts
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momBody.bin"
	DEPENDS
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_SOURCE_DIR}/src/game/adult_female_L_offsets.h"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momBody.bin"
		"-o" "adult_body_F_L"
		"-s" "20.0"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momHead.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momHead.bin"
		"-o" "adult_body_F_L_head"
		"-s" "20.0"
		"--pivot" "bottom"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momArmLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momArmLeft.bin"
		"-o" "adult_body_Arm_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momArmRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momArmRight.bin"
		"-o" "adult_body_Arm_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momLegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momLegLeft.bin"
		"-o" "adult_body_Feet_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/momLegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"${PROJECT_BINARY_DIR}/lander/momLegRight.bin"
		"-o" "adult_body_Feet_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Generate mom character offsets header
add_custom_command(
	OUTPUT "${PROJECT_SOURCE_DIR}/src/game/adult_female_L_offsets.h"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_female_L.obj"
		"--calculate-offsets"
		"-s" "20.0"
		"--offsets-output" "${PROJECT_SOURCE_DIR}/src/game/adult_female_L_offsets.h"
		"--char-name" "ADULT_FEMALE_L"
		"--part" "body=adult_body_F_L" "head=adult_body_F_L_head"
		"arm_left=adult_body_Arm_Left" "arm_right=adult_body_Arm_Right"
		"leg_left=adult_body_Feet_Left" "leg_right=adult_body_Feet_Right"
	VERBATIM
)

# Convert adult_male_L character parts
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadBody.bin"
	DEPENDS
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_SOURCE_DIR}/src/game/adult_male_L_offsets.h"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadBody.bin"
		"-o" "adult_body_F_L.002"
		"-s" "20.0"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadHead.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadHead.bin"
		"-o" "adult_body_M_head.001"
		"-s" "20.0"
		"--pivot" "bottom"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadArmLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadArmLeft.bin"
		"-o" "adult_body_Arm_Left.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadArmRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadArmRight.bin"
		"-o" "adult_body_Arm_Right.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadLegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadLegLeft.bin"
		"-o" "adult_body_Feet_Left.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/dadLegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"${PROJECT_BINARY_DIR}/lander/dadLegRight.bin"
		"-o" "adult_body_Feet_Right.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Generate adult_male_L character offsets header
add_custom_command(
	OUTPUT "${PROJECT_SOURCE_DIR}/src/game/adult_male_L_offsets.h"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/adult_male_L.obj"
		"--calculate-offsets"
		"-s" "20.0"
		"--offsets-output" "${PROJECT_SOURCE_DIR}/src/game/adult_male_L_offsets.h"
		"--char-name" "ADULT_MALE_L"
		"--part" "body=adult_body_F_L.002" "head=adult_body_M_head.001"
		"arm_left=adult_body_Arm_Left.002" "arm_right=adult_body_Arm_Right.002"
		"leg_left=adult_body_Feet_Left.002" "leg_right=adult_body_Feet_Right.002"
	VERBATIM
)

# Convert citizen_1 character parts (kid male)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1Body.bin"
	DEPENDS
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_SOURCE_DIR}/src/game/citizen_1_offsets.h"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1Body.bin"
		"-o" "kid_body_M"
		"-s" "20.0"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1Head.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1Head.bin"
		"-o" "adult_body_M_L_head.001"
		"-s" "20.0"
		"--pivot" "bottom"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1ArmLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1ArmLeft.bin"
		"-o" "kid_body_Arm_Left.004"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1ArmRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1ArmRight.bin"
		"-o" "kid_body_M_Arm_Right.003"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1LegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1LegLeft.bin"
		"-o" "kid_body_Feet_Left.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen1LegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen1LegRight.bin"
		"-o" "kid_body_Feet_Right.002"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Generate citizen_1 character offsets header
add_custom_command(
	OUTPUT "${PROJECT_SOURCE_DIR}/src/game/citizen_1_offsets.h"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_1.obj"
		"--calculate-offsets"
		"-s" "20.0"
		"--offsets-output" "${PROJECT_SOURCE_DIR}/src/game/citizen_1_offsets.h"
		"--char-name" "CITIZEN_1"
		"--part" "body=kid_body_M" "head=adult_body_M_L_head.001"
		"arm_left=kid_body_Arm_Left.004" "arm_right=kid_body_M_Arm_Right.003"
		"leg_left=kid_body_Feet_Left.002" "leg_right=kid_body_Feet_Right.002"
	VERBATIM
)

# Convert citizen_2 character parts (kid female)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2Body.bin"
	DEPENDS
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_SOURCE_DIR}/src/game/citizen_2_offsets.h"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2Body.bin"
		"-o" "kid_body_F_L"
		"-s" "20.0"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2Head.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2Head.bin"
		"-o" "adult_body_M_L_head.002"
		"-s" "20.0"
		"--pivot" "bottom"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2ArmLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2ArmLeft.bin"
		"-o" "kid_body_Arm_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2ArmRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2ArmRight.bin"
		"-o" "kid_body_Arm_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2LegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2LegLeft.bin"
		"-o" "kid_body_Feet_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/citizen2LegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"${PROJECT_BINARY_DIR}/lander/citizen2LegRight.bin"
		"-o" "kid_body_Feet_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Generate citizen_2 character offsets header
add_custom_command(
	OUTPUT "${PROJECT_SOURCE_DIR}/src/game/citizen_2_offsets.h"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/citizen_2.obj"
		"--calculate-offsets"
		"-s" "20.0"
		"--offsets-output" "${PROJECT_SOURCE_DIR}/src/game/citizen_2_offsets.h"
		"--char-name" "CITIZEN_2"
		"--part" "body=kid_body_F_L" "head=adult_body_M_L_head.002"
		"arm_left=kid_body_Arm_Left" "arm_right=kid_body_Arm_Right"
		"leg_left=kid_body_Feet_Left" "leg_right=kid_body_Feet_Right"
	VERBATIM
)

# Convert mask model
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/mask.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/mask.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/mask.obj"
		"${PROJECT_BINARY_DIR}/lander/mask.bin"
		"-o" "mask"
		"-s" "20.0"
	VERBATIM
)

# Convert food box model
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/foodBox.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/food_box.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/food_box.obj"
		"${PROJECT_BINARY_DIR}/lander/foodBox.bin"
		"-o" "food_box"
		"-s" "20.0"
	VERBATIM
)

# Convert enforcer model parts separately for leg animation
# Body + head combined (no animation needed)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/enforcerBody.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
		"${PROJECT_BINARY_DIR}/lander/enforcerBody.bin"
		"-o" "enforcer_body" "enforcer_head"
		"-s" "20.0"
	VERBATIM
)

# Left leg (pivot at top = hip joint)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/enforcerLegLeft.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
		"${PROJECT_BINARY_DIR}/lander/enforcerLegLeft.bin"
		"-o" "enforcer_Feet_Left"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Right leg (pivot at top = hip joint)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/enforcerLegRight.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/enforcer.obj"
		"${PROJECT_BINARY_DIR}/lander/enforcerLegRight.bin"
		"-o" "enforcer_Feet_Right"
		"-s" "20.0"
		"--pivot" "top"
	VERBATIM
)

# Convert house models from OBJ to binary format (with vertex colors)
# House 1
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house1.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_1.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_1.obj"
		"${PROJECT_BINARY_DIR}/lander/house1.bin"
		"-o" "Mercona_hut_2.001"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# House 2
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house2.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_2.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_2.obj"
		"${PROJECT_BINARY_DIR}/lander/house2.bin"
		"-o" "Mercona_hut_2"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# House 3
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house3.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_3.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_3.obj"
		"${PROJECT_BINARY_DIR}/lander/house3.bin"
		"-o" "Mercona_hut_3"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# House interior models (for interior scenes)
# House 1 interior (includes all meshes)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house1_interior.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_1_interior.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_1_interior.obj"
		"${PROJECT_BINARY_DIR}/lander/house1_interior.bin"
		"-o" "Mercona_hut_1_interior" "Mercona_hut_3_interior.002"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# House 2 interior (includes all meshes)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house2_interior.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_2_interior.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_2_interior.obj"
		"${PROJECT_BINARY_DIR}/lander/house2_interior.bin"
		"-o" "Mercona_hut_2_interior" "Mercona_hut_3_interior.003"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# House 3 interior (includes all meshes)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/house3_interior.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/Mercona_hut_3_interior.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/Mercona_hut_3_interior.obj"
		"${PROJECT_BINARY_DIR}/lander/house3_interior.bin"
		"-o" "Mercona_hut_3_interior" "Mercona_hut_3_interior.001"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# Convert tree models from OBJ to binary format
# Large tree (uses tree.002 at larger scale)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/tree_large.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/tree.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/tree.obj"
		"${PROJECT_BINARY_DIR}/lander/tree_large.bin"
		"-o" "tree.002"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# Small tree variant (same model at smaller scale)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/tree_small.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/tree.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/tree.obj"
		"${PROJECT_BINARY_DIR}/lander/tree_small.bin"
		"-o" "tree.002"
		"-s" "50.0"
		"--pivot" "bottom"
	VERBATIM
)

# Convert restaurant models from OBJ to binary format
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/restaurant.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/restuarant.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/restuarant.obj"
		"${PROJECT_BINARY_DIR}/lander/restaurant.bin"
		"-o" "restuarant"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/restaurant_interior.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/restuarant_interior.obj"
	COMMAND
		"${Python3_EXECUTABLE}"
		"${PROJECT_SOURCE_DIR}/tools/convertCharacter.py"
		"${PROJECT_SOURCE_DIR}/assets/restuarant_interior.obj"
		"${PROJECT_BINARY_DIR}/lander/restaurant_interior.bin"
		"-o" "restuarant_interior"
		"-s" "80.0"
		"--pivot" "bottom"
	VERBATIM
)

# Build the lander executable
addPS1Executable(
	lander
	# Engine modules
	src/engine/gpu.c
	src/engine/spu.c
	src/engine/bios.c
	src/engine/model.c
	src/engine/font.c
	src/engine/cdda.c
	src/engine/transform.c
	src/engine/trig.c
	src/engine/camera.c
	# Game modules
	src/game/character.c
	src/game/collision.c
	src/game/input.c
	src/game/main.c
	src/game/rendering.c
	src/game/ui.c
	src/game/world_data.c
)

# Embed texture data into executable
addBinaryFile(lander textureData "${PROJECT_BINARY_DIR}/lander/textureData.dat")

# Embed model data into executable
addBinaryFileWithSize(lander modelData modelData_size "${PROJECT_BINARY_DIR}/lander/modelData.bin")

# Embed font data into executable
addBinaryFile(lander fontTexture "${PROJECT_BINARY_DIR}/lander/fontTexture.dat")
addBinaryFile(lander fontPalette "${PROJECT_BINARY_DIR}/lander/fontPalette.dat")

# Embed grass texture data into executable (32x32 16bpp)
addBinaryFile(lander grassTexture "${PROJECT_BINARY_DIR}/lander/grassTexture.dat")

# Embed title screen texture data into executable (256x256 16bpp)
addBinaryFile(lander titleTexture "${PROJECT_BINARY_DIR}/lander/titleTexture.dat")

# Embed sound effects data into executable (SPU-ADPCM format)
# Music uses CDDA from disc, SFX uses SPU samples
addBinaryFileWithSize(lander sfxBiteData sfxBiteData_size "${PROJECT_BINARY_DIR}/lander/musicData.spu")
addBinaryFileWithSize(lander sfxStepGrassData sfxStepGrassData_size "${PROJECT_BINARY_DIR}/lander/sfx_step_grass.spu")
addBinaryFileWithSize(lander sfxStepWoodData sfxStepWoodData_size "${PROJECT_BINARY_DIR}/lander/sfx_step_wood.spu")

# Embed character body part data into executable
addBinaryFileWithSize(lander charBodyData charBodyData_size "${PROJECT_BINARY_DIR}/lander/charBody.bin")
addBinaryFileWithSize(lander charHeadData charHeadData_size "${PROJECT_BINARY_DIR}/lander/charHead.bin")
addBinaryFileWithSize(lander charArmLeftData charArmLeftData_size "${PROJECT_BINARY_DIR}/lander/charArmLeft.bin")
addBinaryFileWithSize(lander charArmRightData charArmRightData_size "${PROJECT_BINARY_DIR}/lander/charArmRight.bin")
addBinaryFileWithSize(lander charLegLeftData charLegLeftData_size "${PROJECT_BINARY_DIR}/lander/charLegLeft.bin")
addBinaryFileWithSize(lander charLegRightData charLegRightData_size "${PROJECT_BINARY_DIR}/lander/charLegRight.bin")

# Embed mom character body part data into executable
addBinaryFileWithSize(lander momBodyData momBodyData_size "${PROJECT_BINARY_DIR}/lander/momBody.bin")
addBinaryFileWithSize(lander momHeadData momHeadData_size "${PROJECT_BINARY_DIR}/lander/momHead.bin")
addBinaryFileWithSize(lander momArmLeftData momArmLeftData_size "${PROJECT_BINARY_DIR}/lander/momArmLeft.bin")
addBinaryFileWithSize(lander momArmRightData momArmRightData_size "${PROJECT_BINARY_DIR}/lander/momArmRight.bin")
addBinaryFileWithSize(lander momLegLeftData momLegLeftData_size "${PROJECT_BINARY_DIR}/lander/momLegLeft.bin")
addBinaryFileWithSize(lander momLegRightData momLegRightData_size "${PROJECT_BINARY_DIR}/lander/momLegRight.bin")

# Embed food box model
addBinaryFileWithSize(lander foodBoxData foodBoxData_size "${PROJECT_BINARY_DIR}/lander/foodBox.bin")

# Embed mask model
addBinaryFileWithSize(lander maskData maskData_size "${PROJECT_BINARY_DIR}/lander/mask.bin")

# Embed enforcer model parts (body+head, left leg, right leg)
addBinaryFileWithSize(lander enforcerBodyData enforcerBodyData_size "${PROJECT_BINARY_DIR}/lander/enforcerBody.bin")
addBinaryFileWithSize(lander enforcerLegLeftData enforcerLegLeftData_size "${PROJECT_BINARY_DIR}/lander/enforcerLegLeft.bin")
addBinaryFileWithSize(lander enforcerLegRightData enforcerLegRightData_size "${PROJECT_BINARY_DIR}/lander/enforcerLegRight.bin")

# Embed adult male (dad) character body part data into executable
addBinaryFileWithSize(lander dadBodyData dadBodyData_size "${PROJECT_BINARY_DIR}/lander/dadBody.bin")
addBinaryFileWithSize(lander dadHeadData dadHeadData_size "${PROJECT_BINARY_DIR}/lander/dadHead.bin")
addBinaryFileWithSize(lander dadArmLeftData dadArmLeftData_size "${PROJECT_BINARY_DIR}/lander/dadArmLeft.bin")
addBinaryFileWithSize(lander dadArmRightData dadArmRightData_size "${PROJECT_BINARY_DIR}/lander/dadArmRight.bin")
addBinaryFileWithSize(lander dadLegLeftData dadLegLeftData_size "${PROJECT_BINARY_DIR}/lander/dadLegLeft.bin")
addBinaryFileWithSize(lander dadLegRightData dadLegRightData_size "${PROJECT_BINARY_DIR}/lander/dadLegRight.bin")

# Embed citizen_1 character body part data into executable
addBinaryFileWithSize(lander citizen1BodyData citizen1BodyData_size "${PROJECT_BINARY_DIR}/lander/citizen1Body.bin")
addBinaryFileWithSize(lander citizen1HeadData citizen1HeadData_size "${PROJECT_BINARY_DIR}/lander/citizen1Head.bin")
addBinaryFileWithSize(lander citizen1ArmLeftData citizen1ArmLeftData_size "${PROJECT_BINARY_DIR}/lander/citizen1ArmLeft.bin")
addBinaryFileWithSize(lander citizen1ArmRightData citizen1ArmRightData_size "${PROJECT_BINARY_DIR}/lander/citizen1ArmRight.bin")
addBinaryFileWithSize(lander citizen1LegLeftData citizen1LegLeftData_size "${PROJECT_BINARY_DIR}/lander/citizen1LegLeft.bin")
addBinaryFileWithSize(lander citizen1LegRightData citizen1LegRightData_size "${PROJECT_BINARY_DIR}/lander/citizen1LegRight.bin")

# Embed citizen_2 character body part data into executable
addBinaryFileWithSize(lander citizen2BodyData citizen2BodyData_size "${PROJECT_BINARY_DIR}/lander/citizen2Body.bin")
addBinaryFileWithSize(lander citizen2HeadData citizen2HeadData_size "${PROJECT_BINARY_DIR}/lander/citizen2Head.bin")
addBinaryFileWithSize(lander citizen2ArmLeftData citizen2ArmLeftData_size "${PROJECT_BINARY_DIR}/lander/citizen2ArmLeft.bin")
addBinaryFileWithSize(lander citizen2ArmRightData citizen2ArmRightData_size "${PROJECT_BINARY_DIR}/lander/citizen2ArmRight.bin")
addBinaryFileWithSize(lander citizen2LegLeftData citizen2LegLeftData_size "${PROJECT_BINARY_DIR}/lander/citizen2LegLeft.bin")
addBinaryFileWithSize(lander citizen2LegRightData citizen2LegRightData_size "${PROJECT_BINARY_DIR}/lander/citizen2LegRight.bin")

# Embed house model data into executable
addBinaryFileWithSize(lander house1Data house1Data_size "${PROJECT_BINARY_DIR}/lander/house1.bin")
addBinaryFileWithSize(lander house2Data house2Data_size "${PROJECT_BINARY_DIR}/lander/house2.bin")
addBinaryFileWithSize(lander house3Data house3Data_size "${PROJECT_BINARY_DIR}/lander/house3.bin")

# Embed house interior model data into executable
addBinaryFileWithSize(lander house1IntData house1IntData_size "${PROJECT_BINARY_DIR}/lander/house1_interior.bin")
addBinaryFileWithSize(lander house2IntData house2IntData_size "${PROJECT_BINARY_DIR}/lander/house2_interior.bin")
addBinaryFileWithSize(lander house3IntData house3IntData_size "${PROJECT_BINARY_DIR}/lander/house3_interior.bin")

# Embed tree model data into executable
addBinaryFileWithSize(lander treeLargeData treeLargeData_size "${PROJECT_BINARY_DIR}/lander/tree_large.bin")
addBinaryFileWithSize(lander treeSmallData treeSmallData_size "${PROJECT_BINARY_DIR}/lander/tree_small.bin")

# Embed restaurant model data into executable
addBinaryFileWithSize(lander restaurantData restaurantData_size "${PROJECT_BINARY_DIR}/lander/restaurant.bin")
addBinaryFileWithSize(lander restaurantIntData restaurantIntData_size "${PROJECT_BINARY_DIR}/lander/restaurant_interior.bin")

# Web build target - creates proper PS1 CD image with mkpsxiso
# Creates lander.rom (zip of .bin+.cue) and web_build.zip (full distribution for itch.io)
set(MKPSXISO "${PROJECT_SOURCE_DIR}/tools/mkpsxiso/mkpsxiso.exe")
set(ISO_WORK_DIR "${PROJECT_BINARY_DIR}/iso_work")
set(WEB_ROM_DIR "${PROJECT_SOURCE_DIR}/web/rom")

add_custom_target(build-web
	DEPENDS lander.psexe
	COMMAND ${CMAKE_COMMAND} -E echo "Creating web distribution with CD audio..."
	# Create directories
	COMMAND ${CMAKE_COMMAND} -E make_directory "${ISO_WORK_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${WEB_ROM_DIR}"
	# Copy psexe to iso work directory
	COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_BINARY_DIR}/lander.psexe" "${ISO_WORK_DIR}/lander.psexe"
	# Copy audio tracks
	COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/assets/Music/Intro_V3.wav" "${ISO_WORK_DIR}/track02_intro.wav"
	COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/assets/Music/Gameplay_Loop.wav" "${ISO_WORK_DIR}/track03_gameplay.wav"
	# Run mkpsxiso to create proper CD image
	COMMAND ${CMAKE_COMMAND} -E chdir "${ISO_WORK_DIR}" "${MKPSXISO}" -y iso.xml
	# Copy bin/cue to web/rom
	COMMAND ${CMAKE_COMMAND} -E copy "${ISO_WORK_DIR}/revenants_of_elmoria.bin" "${WEB_ROM_DIR}/revenants_of_elmoria.bin"
	COMMAND ${CMAKE_COMMAND} -E copy "${ISO_WORK_DIR}/revenants_of_elmoria.cue" "${WEB_ROM_DIR}/revenants_of_elmoria.cue"
	# Create revenants_of_elmoria.rom (zip of bin+cue for EmulatorJS)
	COMMAND ${CMAKE_COMMAND} -E chdir "${WEB_ROM_DIR}" ${CMAKE_COMMAND} -E tar cf "${WEB_ROM_DIR}/revenants_of_elmoria.rom" --format=zip revenants_of_elmoria.bin revenants_of_elmoria.cue
	# Create revenants_of_elmoria.zip (standalone game distribution with bin+cue)
	COMMAND ${CMAKE_COMMAND} -E chdir "${WEB_ROM_DIR}" ${CMAKE_COMMAND} -E tar cf "${PROJECT_BINARY_DIR}/revenants_of_elmoria.zip" --format=zip revenants_of_elmoria.bin revenants_of_elmoria.cue
	# Create full web distribution zip with ALL required files for itch.io
	COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/web" ${CMAKE_COMMAND} -E tar cf "${PROJECT_BINARY_DIR}/web_build.zip" --format=zip index.html bios data emulatorjs rom
	COMMAND ${CMAKE_COMMAND} -E echo "Web build complete!"
	COMMAND ${CMAKE_COMMAND} -E echo "  ROM: ${WEB_ROM_DIR}/revenants_of_elmoria.rom"
	COMMAND ${CMAKE_COMMAND} -E echo "  Game zip: ${PROJECT_BINARY_DIR}/revenants_of_elmoria.zip"
	COMMAND ${CMAKE_COMMAND} -E echo "  Web package: ${PROJECT_BINARY_DIR}/web_build.zip"
	VERBATIM
)

