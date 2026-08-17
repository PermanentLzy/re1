import json

with open('performance_results.json') as f:
    data = json.load(f)

tests = data['tests']
sorted_tests = sorted(tests.items(), key=lambda x: x[1].get('improvement', {}).get('code_reduction_percent', 0))

print("Tests with lowest code size reduction:")
for name, result in sorted_tests[:10]:
    if 'improvement' in result:
        print(f"  {name}: {result['improvement']['code_reduction_percent']:.1f}% reduction")
        print(f"    Original: {result['original']['asm_lines']} lines, Optimized: {result['optimized']['asm_lines']} lines")
