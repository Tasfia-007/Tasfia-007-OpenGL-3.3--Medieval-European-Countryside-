import wave
import struct
import math
import random

def generate_thunder(filename="textures/thunder.wav"):
    sample_rate = 44100
    duration_s = 2.0
    num_samples = int(duration_s * sample_rate)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        # Simple thunder envelope + lowpass random noise
        last_val = 0.0
        for i in range(num_samples):
            t = i / sample_rate
            # Random noise
            noise = random.uniform(-1.0, 1.0)
            
            # Simple 1-pole lowpass filter to make it rumbly
            # cutoff frequency varies over time to simulate rolling thunder
            cutoff = 200 + 100 * math.sin(t * 3.14)
            alpha = cutoff / (cutoff + sample_rate / (2 * math.pi))
            
            val = alpha * noise + (1 - alpha) * last_val
            last_val = val
            
            # Envelope (sudden strike, rolling fade)
            envelope = 0.0
            if t < 0.1:
                envelope = t / 0.1
            else:
                envelope = math.exp(-(t - 0.1) * 2.5) * (1.0 + 0.3 * math.sin(t * 15.0))
                
            sample = val * envelope * 2.0 # boost volume slightly
            
            # Clipping
            if sample > 1.0: sample = 1.0
            elif sample < -1.0: sample = -1.0
            
            # Write to file
            wav_file.writeframes(struct.pack('<h', int(sample * 32767.0)))

if __name__ == "__main__":
    generate_thunder()
