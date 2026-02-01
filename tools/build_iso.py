#!/usr/bin/env python3
"""
Build a PSX disc image (.bin/.cue) from the compiled executable.
Includes CD-DA audio tracks for music and SPU for sound effects.
Requires: mkpsxiso

Audio tracks:
  Track 1: Data (game executable)
  Track 2: Intro music (plays during intro sequence)
  Track 3: Gameplay loop (plays during gameplay, loops)
"""

import sys
import subprocess
import shutil
import wave
import struct
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
OUTPUT_DIR = PROJECT_ROOT / "web" / "rom"
TOOLS_DIR = PROJECT_ROOT / "tools"
ASSETS_DIR = PROJECT_ROOT / "assets"
MUSIC_DIR = ASSETS_DIR / "Music"

def convert_to_cdda(input_path, output_path):
    """Convert WAV file to CD-DA format (16-bit stereo 44100Hz PCM)."""
    print(f"  Converting {input_path.name} to CD-DA format...")

    with wave.open(str(input_path), 'rb') as wav_in:
        channels = wav_in.getnchannels()
        sample_width = wav_in.getsampwidth()
        framerate = wav_in.getframerate()
        n_frames = wav_in.getnframes()

        print(f"    Input: {channels}ch, {sample_width*8}bit, {framerate}Hz")

        # Read all audio data
        audio_data = wav_in.readframes(n_frames)

    # Convert to samples based on input format
    if sample_width == 2:  # 16-bit
        fmt = f'<{len(audio_data)//2}h'
        samples = list(struct.unpack(fmt, audio_data))
    elif sample_width == 3:  # 24-bit
        samples = []
        for i in range(0, len(audio_data), 3):
            # 24-bit little-endian to signed int, then scale to 16-bit
            val = audio_data[i] | (audio_data[i+1] << 8) | (audio_data[i+2] << 16)
            if val >= 0x800000:  # Sign extend
                val -= 0x1000000
            samples.append(val >> 8)  # Scale 24-bit to 16-bit
    elif sample_width == 4:  # 32-bit
        fmt = f'<{len(audio_data)//4}i'
        raw_samples = struct.unpack(fmt, audio_data)
        samples = [s >> 16 for s in raw_samples]  # Scale to 16-bit
    else:
        raise ValueError(f"Unsupported sample width: {sample_width}")

    # Convert mono to stereo if needed
    if channels == 1:
        stereo_samples = []
        for s in samples:
            stereo_samples.extend([s, s])  # Duplicate for L and R
        samples = stereo_samples
        channels = 2

    # Resample to 44100Hz if needed
    if framerate != 44100:
        # Simple linear interpolation resampling
        ratio = framerate / 44100
        new_length = int(len(samples) / ratio / channels) * channels
        resampled = []
        for i in range(0, new_length, channels):
            src_idx = int((i // channels) * ratio) * channels
            if src_idx + channels <= len(samples):
                for c in range(channels):
                    resampled.append(samples[src_idx + c])
            else:
                for c in range(channels):
                    resampled.append(0)
        samples = resampled
        print(f"    Resampled from {framerate}Hz to 44100Hz")

    # Write output as 16-bit stereo 44100Hz
    with wave.open(str(output_path), 'wb') as wav_out:
        wav_out.setnchannels(2)
        wav_out.setsampwidth(2)  # 16-bit
        wav_out.setframerate(44100)

        # Clamp samples to 16-bit range
        clamped = [max(-32768, min(32767, int(s))) for s in samples]
        wav_out.writeframes(struct.pack(f'<{len(clamped)}h', *clamped))

    out_size = output_path.stat().st_size / (1024 * 1024)
    print(f"    Output: 2ch, 16bit, 44100Hz ({out_size:.1f} MB)")

def create_iso_xml(audio_tracks=None):
    """Create XML for disc with optional CD-DA audio tracks.
    audio_tracks: list of source filenames in work_dir
    """
    xml = '''<?xml version="1.0" encoding="UTF-8"?>

<iso_project image_name="lander.bin" cue_sheet="lander.cue">
\t<track type="data">
\t\t<directory_tree>
\t\t\t<file name="SYSTEM.CNF" source="system.cnf"/>
\t\t\t<file name="LANDER.EXE" source="lander.psexe"/>
\t\t</directory_tree>
\t</track>
'''
    if audio_tracks:
        for source in audio_tracks:
            xml += f'''\t<track type="audio" source="{source}"/>
'''
    xml += '''</iso_project>
'''
    return xml

def create_system_cnf():
    """Create SYSTEM.CNF boot configuration."""
    return "BOOT=cdrom:\\LANDER.EXE;1\nTCB=4\nEVENT=10\nSTACK=801FFFF0\n"

def main():
    # Find mkpsxiso
    mkpsxiso = None
    possible_mkpsxiso = [
        PROJECT_ROOT / "web" / "rom" / "PSX Snake Alpha Source Code and Assets" / "Source Code and Assets" / "Source Code" / "mkpsxiso" / "mkpsxiso.exe",
        TOOLS_DIR / "mkpsxiso" / "mkpsxiso.exe",
        TOOLS_DIR / "mkpsxiso.exe",
        Path("C:/mkpsxiso/bin/mkpsxiso.exe"),
    ]
    for p in possible_mkpsxiso:
        if p.exists():
            mkpsxiso = str(p)
            break
    if not mkpsxiso:
        mkpsxiso = shutil.which("mkpsxiso")

    if not mkpsxiso:
        print("ERROR: mkpsxiso not found!")
        sys.exit(1)
    print(f"Found mkpsxiso: {mkpsxiso}")

    # Check for compiled executable
    exe_path = BUILD_DIR / "lander.psexe"
    if not exe_path.exists():
        print(f"ERROR: {exe_path} not found!")
        print("Run 'cmake --build build' first")
        sys.exit(1)

    # Create working directory
    work_dir = BUILD_DIR / "iso_work"
    work_dir.mkdir(exist_ok=True)

    # Copy executable
    shutil.copy(exe_path, work_dir / "lander.psexe")
    exe_size = exe_path.stat().st_size / 1024
    print(f"Executable size: {exe_size:.1f} KB")

    # Check for CD-DA music files and convert to proper format
    audio_tracks = []

    # Track 2: Intro music
    intro_music = MUSIC_DIR / "Intro_V3.wav"
    if intro_music.exists():
        track02_path = work_dir / "track02_intro.wav"
        convert_to_cdda(intro_music, track02_path)
        audio_tracks.append("track02_intro.wav")
        print(f"Track 2 (Intro): Ready")
    else:
        print(f"WARNING: Intro music not found: {intro_music}")

    # Track 3: Gameplay loop
    gameplay_music = MUSIC_DIR / "Gameplay_Loop.wav"
    if gameplay_music.exists():
        track03_path = work_dir / "track03_gameplay.wav"
        convert_to_cdda(gameplay_music, track03_path)
        audio_tracks.append("track03_gameplay.wav")
        print(f"Track 3 (Gameplay): Ready")
    else:
        print(f"WARNING: Gameplay music not found: {gameplay_music}")

    if audio_tracks:
        print(f"CD-DA: {len(audio_tracks)} audio track(s) will be included")
    else:
        print("No CD-DA music tracks found")

    # Create SYSTEM.CNF
    (work_dir / "system.cnf").write_text(create_system_cnf())

    # Create ISO XML config
    xml_path = work_dir / "iso.xml"
    xml_path.write_text(create_iso_xml(audio_tracks))

    # Run mkpsxiso
    print("Building disc image...")

    import time
    timestamp = int(time.time())
    temp_bin = f"lander_{timestamp}.bin"
    temp_cue = f"lander_{timestamp}.cue"

    # Update XML to use temp filenames
    xml_content = xml_path.read_text()
    xml_content = xml_content.replace('image_name="lander.bin"', f'image_name="{temp_bin}"')
    xml_content = xml_content.replace('cue_sheet="lander.cue"', f'cue_sheet="{temp_cue}"')
    xml_path.write_text(xml_content)

    cmd = f'"{mkpsxiso}" iso.xml -y'
    result = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True, shell=True)

    if result.returncode != 0:
        print("ERROR: mkpsxiso failed!")
        print(result.stdout)
        print(result.stderr)
        sys.exit(1)

    # Copy output to web/rom
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    bin_path = work_dir / temp_bin
    cue_path = work_dir / temp_cue

    if bin_path.exists():
        dest_bin = OUTPUT_DIR / "lander.bin"
        dest_cue = OUTPUT_DIR / "lander.cue"

        for dest in [dest_bin, dest_cue]:
            if dest.exists():
                try:
                    dest.unlink()
                except PermissionError:
                    print(f"ERROR: {dest} is locked!")
                    sys.exit(1)

        shutil.copy(bin_path, dest_bin)

        cue_content = cue_path.read_text()
        cue_content = cue_content.replace(temp_bin, "lander.bin")
        dest_cue.write_text(cue_content)

        # Clean up temp files
        try:
            bin_path.unlink()
            cue_path.unlink()
        except:
            pass

        bin_size = dest_bin.stat().st_size / (1024 * 1024)
        print(f"Created: {dest_bin} ({bin_size:.1f} MB)")
        print(f"Created: {dest_cue}")

        # Create zip for EmulatorJS
        import zipfile
        dest_zip = OUTPUT_DIR / "lander.zip"
        try:
            if dest_zip.exists():
                dest_zip.unlink()
            with zipfile.ZipFile(dest_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
                zf.write(dest_bin, "lander.bin")
                zf.write(dest_cue, "lander.cue")
            zip_size = dest_zip.stat().st_size / (1024 * 1024)
            print(f"Created: {dest_zip} ({zip_size:.1f} MB)")
        except Exception as e:
            print(f"Warning: Could not create zip: {e}")
    else:
        print("ERROR: Output files not created")
        sys.exit(1)

    print("\nDone!")
    print("CD-DA Track Layout:")
    print("  Track 1: Data (game)")
    if len(audio_tracks) >= 1:
        print("  Track 2: Intro music")
    if len(audio_tracks) >= 2:
        print("  Track 3: Gameplay loop")

if __name__ == "__main__":
    main()
