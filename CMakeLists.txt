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
# Torso (kid_body_F_L)
add_custom_command(
	OUTPUT "${PROJECT_BINARY_DIR}/lander/charBody.bin"
	DEPENDS "${PROJECT_SOURCE_DIR}/assets/kids_female_L.obj"
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
	src/game/main.c
	src/game/world_data.c
)

# Embed texture data into executable
addBinaryFile(lander textureData "${PROJECT_BINARY_DIR}/lander/textureData.dat")

# Embed model data into executable
addBinaryFileWithSize(lander modelData modelData_size "${PROJECT_BINARY_DIR}/lander/modelData.bin")

# Embed font data into executable
addBinaryFile(lander fontTexture "${PROJECT_BINARY_DIR}/lander/fontTexture.dat")
addBinaryFile(lander fontPalette "${PROJECT_BINARY_DIR}/lander/fontPalette.dat")

# Embed music data into executable (SPU-ADPCM format)
addBinaryFileWithSize(lander musicData musicData_size "${PROJECT_BINARY_DIR}/lander/musicData.spu")

# Embed character body part data into executable
addBinaryFileWithSize(lander charBodyData charBodyData_size "${PROJECT_BINARY_DIR}/lander/charBody.bin")
addBinaryFileWithSize(lander charHeadData charHeadData_size "${PROJECT_BINARY_DIR}/lander/charHead.bin")
addBinaryFileWithSize(lander charArmLeftData charArmLeftData_size "${PROJECT_BINARY_DIR}/lander/charArmLeft.bin")
addBinaryFileWithSize(lander charArmRightData charArmRightData_size "${PROJECT_BINARY_DIR}/lander/charArmRight.bin")
addBinaryFileWithSize(lander charLegLeftData charLegLeftData_size "${PROJECT_BINARY_DIR}/lander/charLegLeft.bin")
addBinaryFileWithSize(lander charLegRightData charLegRightData_size "${PROJECT_BINARY_DIR}/lander/charLegRight.bin")

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

