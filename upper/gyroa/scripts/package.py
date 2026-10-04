"""Package only explicit release directories, excluding local dependencies/accounts."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parents[1] / 'artifacts/web-host/gyroa-20261004-startup1'
DOWNLOADS = Path('E:/Downlload')
VERSION = '20261004-startup1'
manifest = json.loads((ROOT / 'dist/asset-manifest.json').read_text(encoding='utf-8'))
for name, expected in manifest['files'].items():
    for directory in ['src/public', 'dist/public']:
        path = ROOT / directory / name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, path
(ROOT / 'hashcheck.txt').write_text(
    'Local build 20261004startup1; target gyroa.233688.xyz; NOT deployed\n' +
    ''.join(f'{sha}  /{name}\n' for name, sha in sorted(manifest['files'].items())), encoding='utf-8')
OUT.mkdir(parents=True, exist_ok=True)

def tree(directory):
    base = ROOT / directory
    for path in sorted(base.rglob('*')):
        if path.is_symlink() or path.is_junction():
            raise RuntimeError(f'Unexpected link: {path}')
        if path.is_file() and '__pycache__' not in path.parts:
            yield path

def archive(kind, dirs, names):
    output = OUT / f'gyroa-{kind}-{VERSION}.zip'
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for directory in dirs:
            for path in tree(directory):
                z.write(path, 'gyroa/' + path.relative_to(ROOT).as_posix())
        for name in names:
            if kind == 'deploy' and name == 'package.json':
                package = json.loads((ROOT / name).read_text(encoding='utf-8'))
                package['scripts'] = {k: package['scripts'][k] for k in ['dev', 'deploy']}
                z.writestr('gyroa/package.json', json.dumps(package, indent=2) + '\n')
            elif kind == 'deploy' and name == 'README.md':
                z.writestr('gyroa/README.md', '''# gyroa 预构建部署包 · 20261004startup1

目标：https://gyroa.233688.xyz/。本包未发布，静态资源已构建。
Node.js ≥22.12.0；解压后进入 gyroa 目录：

```sh
npm ci
npm run dev
# 登录目标 Cloudflare 账户后发布：
npm run deploy
```

Worker 入口 src/index.js，静态目录 dist/public，路由沿用原压缩包。
配置 v4 启动窗口 0～60 秒，默认 2 秒；0 秒使用历史。
升级页可显示型号 AT32；兼容旧固件。共享窗口需新版 BL+APP，网页只升级 APP。
本包用于直接发布；可编辑页面源码和回归测试请用同版本源码包。
asset-manifest.json 与 hashcheck.txt 提供静态文件校验。
''')
            else:
                z.write(ROOT / name, 'gyroa/' + name)
    with zipfile.ZipFile(output) as z:
        assert z.testzip() is None
        for name, sha in manifest['files'].items():
            assert hashlib.sha256(z.read('gyroa/dist/public/' + name)).hexdigest() == sha
        assert all('node_modules' not in n and '..' not in Path(n).parts for n in z.namelist())
        # The original worker's route and logic are retained in each release.
        assert z.read('gyroa/src/index.js') == (ROOT / 'src/index.js').read_bytes()
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    shutil.copy2(output, DOWNLOADS / output.name)
    assert output.read_bytes() == (DOWNLOADS / output.name).read_bytes()
    return output

common = ['package.json', 'package-lock.json', 'wrangler.jsonc', 'README.md', 'hashcheck.txt', 'dist/asset-manifest.json']
source = archive('src', ['src', 'dist/public', 'test', 'scripts'], common)
deploy = archive('deploy', ['dist/public'], ['src/index.js'] + common)
checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in [source, deploy])
(OUT / 'SHA256SUMS.txt').write_text(checksums, encoding='ascii')
for path in [source, deploy]:
    print(f'{path} ({path.stat().st_size} bytes)')
print(checksums)
