"""Static contract checks only; never loads a DLL or launches BDS."""
import argparse
import pathlib
import re
import subprocess
import json

parser = argparse.ArgumentParser()
parser.add_argument('--clang', type=pathlib.Path)
args = parser.parse_args()
base = pathlib.Path(__file__).resolve().parents[1]
header = base / 'Core' / 'core_abi.h'
source = header.read_text(encoding='utf-8')
code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
boundary = re.sub(r'^EC_STATIC_ASSERT[^\n]*', '', code, flags=re.M)

for forbidden in ('std::', 'std::function', 'std::string', 'size_t', 'void*', 'void *', 'throw ', 'new ', 'delete ', 'sqlite3', 'nlohmann', 'MintCapability', 'caller_plugin_id'):
    assert forbidden not in boundary, 'Forbidden public boundary: ' + forbidden
assert re.findall(r'#include\s+[<"]([^>"]+)[>"]', code) == ['stddef.h', 'stdint.h']
assert '#define EC_PHASE1_FEATURES UINT64_C(0)' in code
assert '#define EC_COIN_MINOR_UNITS_PER_LIANG INT64_C(100)' in code
assert 'EC_EXPORT const EternalCoreApi* EC_CALL EternalCore_QueryApi(' in code

expected_sizes = {
    'EcId128': 16, 'EcUuid': 16, 'EcUtf8View': 16, 'EcUtf8Buffer': 16,
    'EcVersionInfo': 48, 'EcFeatureInfo': 32, 'EcIdentityRequest': 48,
    'EcIdentitySnapshot': 72, 'EcCoinRequest': 40, 'EcCoinSnapshot': 40,
    'EcTransferRequest': 112, 'EcTransferSubmission': 48,
    'EcReceiptRequest': 48, 'EcTransferReceipt': 88, 'EternalCoreApi': 96,
}
for name, size in expected_sizes.items():
    assert re.search(r'EC_STATIC_ASSERT\(sizeof\(' + name + r'\)\s*==\s*' + str(size) + r'\b', code), name
table = re.search(r'typedef struct EternalCoreApi \{(.*?)\} EternalCoreApi;', code, re.S).group(1)
methods = re.findall(r'Ec\w+Fn\s+(\w+);', table)
assert methods == ['get_version', 'get_features', 'read_identity', 'read_coin', 'submit_transfer', 'poll_receipt']

result = {'staticSourceChecks': True, 'declaredStructLayouts': len(expected_sizes),
          'phase1FeatureBits': 0, 'minorUnitsPerLiang': 100,
          'c11Compiled': False, 'cpp20Compiled': False, 'dllLoaded': False, 'bdsStarted': False}
for phase_header in [base / 'Core' / 'phase2_abi.h', base / 'Core' / 'native_ingress_abi.h']:
    phase_source = phase_header.read_text(encoding='utf-8')
    phase_code = re.sub(r'/\*.*?\*/|//[^\n]*', '', phase_source, flags=re.S)
    for forbidden in ('std::', 'std::function', 'std::string', 'throw ', 'new ', 'delete ', 'sqlite3', 'nlohmann'):
        assert forbidden not in phase_code, 'Forbidden Phase2 boundary: ' + forbidden
    assert re.findall(r'#include\s+[<"]([^>"]+)[>"]', phase_code) in [['core_abi.h'], ['phase2_abi.h']]
phase_code = (base / 'Core' / 'phase2_abi.h').read_text(encoding='utf-8')
assert '#define EC_PHASE2_PRODUCTION_FEATURES UINT64_C(0)' in phase_code
assert 'EternalCoreApi v1_0;' in phase_code
result['phase2SourceChecks'] = True
if args.clang:
    assert args.clang.is_file(), 'Clang executable not found'
    for probe in [base / 'tests' / 'abi_layout.c', base / 'tests' / 'module_abi_layout.c',
                  base / 'tests' / 'phase2_abi_layout.c', base / 'tests' / 'native_ingress_layout.c']:
        for language, standard, key in [('c', 'c11', 'c11Compiled'), ('c++', 'c++20', 'cpp20Compiled')]:
            command = [str(args.clang), '--target=x86_64-pc-windows-msvc', '-ffreestanding',
                       '-fsyntax-only', '-Wall', '-Wextra', '-Werror', '-x', language,
                       '-std=' + standard, str(probe)]
            run = subprocess.run(command, check=False, capture_output=True, text=True)
            if run.returncode:
                raise RuntimeError(str(probe) + ' ' + language + ' ABI compile failed:\n' + run.stdout + run.stderr)
            result[key] = True
    result['moduleAbiLayoutsCompiled'] = True
    result['phase2AndNativeLayoutsCompiled'] = True
print(json.dumps(result))
