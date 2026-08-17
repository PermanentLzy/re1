import json

with open('performance_results.json') as f:
    data = json.load(f)

tests = data['tests']
for name, result in sorted(tests.items()):
    if 'improvement' in result:
        orig = result['original']['asm_lines']
        opt = result['optimized']['asm_lines']
        reduction = result['improvement']['code_reduction_percent']
        speedup = result['improvement']['speedup_percent']
        print(f"{name:35s} {orig:4d} → {opt:4d} lines ({reduction:5.1f}% reduction, {speedup:5.1f}% speedup)")

# Summary
summary = data['summary']
print(f"\nAverage code reduction: {summary['avg_code_reduction']:.1f}%")
print(f"Average speedup: {summary['avg_speedup']:.1f}%")
print(f"Score: {summary['performance_score']:.1f}/100")
