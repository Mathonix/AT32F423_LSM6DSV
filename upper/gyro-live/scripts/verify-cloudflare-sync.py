"""Independently compare public HTTPS assets with the reviewed deployment manifest."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone, timedelta
import hashlib
import json
from pathlib import Path
import re
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT.parents[1] / 'artifacts/web-host'
VERSION = '20261001fw1'


def fetch(path):
    request = urllib.request.Request('https://gyro.233688.xyz/' + path,
                                     headers={'User-Agent': 'Mozilla/5.0', 'Cache-Control': 'no-cache'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', default=VERSION)
    parser.add_argument('--before', action='store_true', help='Save current online rollback assets; allow differences.')
    args = parser.parse_args()
    version = args.version
    manifest = json.loads((ARTIFACTS / f'deployment-manifest-{version}.json').read_text(encoding='utf-8'))
    mode = 'before' if args.before else 'after'
    output = ARTIFACTS / f'online-{mode}-sync-{version}'
    output.mkdir(parents=True, exist_ok=True)

    def verify(asset):
        status, data = fetch(asset['file'])
        dest = output / asset['file']
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        exact = status == 200 and len(data) == asset['bytes'] and digest == asset['sha256']
        result = {'file': asset['file'], 'status': status, 'bytes': len(data), 'sha256': digest,
                  'wireExactMatch': exact, 'match': exact}
        if asset['file'] == 'index.html' and not exact:
            # Cloudflare adds this analytics module at the edge, including on the previous release.
            # Accept only this specific addition; any other HTML difference still fails the check.
            pattern = rb'\n<script type="module" src="https://static\.cloudflareinsights\.com/beacon\.min\.js/[^"<>]+" integrity="[^"<>]+" data-cf-beacon=\'[^\'<>]+\' crossorigin="anonymous"></script>'
            clean, count = re.subn(pattern, b'', data)
            result['edgeAnalyticsScripts'] = count
            result['sourceBytes'] = len(clean)
            result['sourceSha256'] = hashlib.sha256(clean).hexdigest()
            result['match'] = count == 1 and len(clean) == asset['bytes'] and result['sourceSha256'] == asset['sha256']
        return result

    with ThreadPoolExecutor(max_workers=4) as pool:
        assets = list(pool.map(verify, manifest['assets']))
    status, data = fetch('healthz')
    health = json.loads(data)
    html = (output / 'index.html').read_text(encoding='utf-8')
    report = {
        'checkedAtBeijing': datetime.now(timezone(timedelta(hours=8))).isoformat(),
        'url': 'https://gyro.233688.xyz/', 'expectedAssetVersion': version,
        'htmlVersions': sorted(set(re.findall(r'\?v=([A-Za-z0-9-]+)', html))),
        'health': {'status': status, 'body': health}, 'assets': assets,
        'passed': all(a['match'] for a in assets) and status == 200 and health.get('ok') is True,
    }
    report_path = ARTIFACTS / f'online-{mode}-sync-{version}.json'
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not args.before and not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
