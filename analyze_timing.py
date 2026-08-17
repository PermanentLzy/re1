import json

with open('performance_results.json') as f:
    data = json.load(f)

tests = data['tests']
print("Timing analysis (showing tests with negative speedup > 10%):")
print("=" * 70)

for name, result in sorted(tests.items()):
    if 'improvement' in result:
        orig_time = result['original']['compile_time']
        opt_time = result['optimized']['compile_time']
        speedup = result['improvement']['speedup_percent']
        if speedup < -10:
            print(f"{name:35s} orig: {orig_time:.4f}s  opt: {opt_time:.4f}s  speedup: {speedup:5.1f}%")

print("\nTiming analysis (showing tests with positive speedup):")
print("=" * 70)
for name, result in sorted(tests.items()):
    if 'improvement' in result:
        orig_time = result['original']['compile_time']
        opt_time = result['optimized']['compile_time']
        speedup = result['improvement']['speedup_percent']
        if speedup > 5:
            print(f"{name:35s} orig: {orig_time:.4f}s  opt: {opt_time:.4f}s  speedup: {speedup:5.1f}%")
