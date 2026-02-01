#!/usr/bin/env python3
"""
Trim audio file to specified duration.
Usage: trimAudio.py <input.wav> <output.wav> <seconds>
"""

import sys
import wave
import struct

def trim_audio(input_path, output_path, duration_seconds):
    """Trim audio file to specified duration and convert to mono 22050Hz."""
    with wave.open(input_path, 'rb') as wav_in:
        channels = wav_in.getnchannels()
        sample_width = wav_in.getsampwidth()
        framerate = wav_in.getframerate()
        n_frames = wav_in.getnframes()

        # Calculate frames to read
        max_frames = int(duration_seconds * framerate)
        frames_to_read = min(max_frames, n_frames)

        # Read audio data
        audio_data = wav_in.readframes(frames_to_read)

    # Convert to mono if stereo
    if channels == 2 and sample_width == 2:
        # 16-bit stereo -> mono
        samples = struct.unpack(f'<{len(audio_data)//2}h', audio_data)
        mono_samples = []
        for i in range(0, len(samples), 2):
            if i + 1 < len(samples):
                # Average left and right channels
                mono_samples.append((samples[i] + samples[i + 1]) // 2)
            else:
                mono_samples.append(samples[i])
        audio_data = struct.pack(f'<{len(mono_samples)}h', *mono_samples)
        channels = 1
        frames_to_read = len(mono_samples)

    # Write output
    with wave.open(output_path, 'wb') as wav_out:
        wav_out.setnchannels(1)  # Mono output
        wav_out.setsampwidth(sample_width)
        wav_out.setframerate(framerate)
        wav_out.writeframes(audio_data)

    print(f"Trimmed {input_path} to {duration_seconds}s -> {output_path}")
    print(f"  Original: {n_frames} frames @ {framerate}Hz, {channels}ch")
    print(f"  Output: {frames_to_read} frames, mono")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.wav> <output.wav> <seconds>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    duration = float(sys.argv[3])

    trim_audio(input_path, output_path, duration)
