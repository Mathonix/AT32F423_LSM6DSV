"""Package only reviewed host files; never include device firmware or credentials."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT.parents[1] / 'artifacts' / 'web-host'
VERSION = '20261001fw1'
ASSETS = ['app.js', 'firmware-upgrade.js', 'index.html', 'models/pcb-source.json', 'models/pcb.glb',
          'pcb-view.js', 'style.css', 'THIRD-PARTY-NOTICES.txt']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', default=VERSION)
    parser.add_argument('--previous-version', default='3b531072-4740-48ad-b386-ee7074838897')
    parser.add_argument('--previous-assets', default='20261001acc3')
    args = parser.parse_args()
    version = args.version
    public = ROOT / 'dist/public'
    actual = sorted(p.relative_to(public).as_posix() for p in public.rglob('*') if p.is_file())
    assert actual == sorted(ASSETS), f'Unexpected deployment assets: {actual}'
    assets = []
    for name in ASSETS:
        data = (public / name).read_bytes()
        assert data == (ROOT / 'src/public' / name).read_bytes(), f'Stale build: {name}'
        assets.append({'file': name, 'bytes': len(data), 'sha256': sha(data)})
    worker = (ROOT / 'dist/worker/index.js').read_bytes()
    assert worker == (ROOT / 'backup/live-deployed/index.js').read_bytes()
    assert version.encode() in (public / 'index.html').read_bytes()
    manifest = {
        'assetVersion': version, 'worker': 'gyro-233688', 'domain': 'gyro.233688.xyz',
        'previousReportedVersion': args.previous_version,
        'previousReportedAssetVersion': args.previous_assets,
        'rollbackNote': 'Record the actual current Cloudflare version before publishing.',
        'workerScript': {'bytes': len(worker), 'sha256': sha(worker), 'changed': False},
        'assets': assets,
    }
    manifest_data = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    (ARTIFACTS / f'deployment-manifest-{version}.json').write_bytes(manifest_data)
    package = ARTIFACTS / f'gyro-233688-{version}.zip'
    # Explicit allowlist: no application BIN/HEX, Flash backups, auth/config caches, or node_modules.
    project_files = ['src/index.js', 'wrangler.jsonc', 'package.json', 'package-lock.json',
                     'docs/deployment-request.txt']
    with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in ASSETS:
            z.write(public / name, 'dist/public/' + name)
        z.write(ROOT / 'dist/worker/index.js', 'dist/worker/index.js')
        for name in project_files:
            z.write(ROOT / name, name)
        z.writestr('deployment-manifest.json', manifest_data)
    with zipfile.ZipFile(package) as z:
        for item in assets:
            assert sha(z.read('dist/public/' + item['file'])) == item['sha256']
    print(json.dumps({'package': str(package), 'bytes': package.stat().st_size,
                      'sha256': sha(package.read_bytes()), 'assets': len(assets)}, indent=2))


if __name__ == '__main__':
    main()
