#!/usr/bin/env python3
"""Build an EASIROC-only release under the development repository (no ODB access)."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import shutil
import shlex
import subprocess

# Works both as a direct script and when imported by unit tests.
if __package__:
    from . import deploy_easiroc as deploy
else:
    import deploy_easiroc as deploy


if __package__:
    from .midas_build import link_libraries
else:
    from midas_build import link_libraries


def prepare(output, midas, node):
    repo = Path(__file__).resolve().parents[2]
    deploy.require(repo == Path('/home/nagao/midas/midas/online'), 'Prepare only in the development repository')
    output = output.absolute()
    deploy.require(output.is_relative_to(repo / 'easiroc_frontend/build') and
                   not output.exists(), 'Use a new directory under easiroc_frontend/build')
    for parent in output.parents:
        deploy.require(not parent.is_symlink(), 'Symlink output parent refused')
    dependencies = {name: deploy.digest(deploy.regular(midas / 'lib' / name))
                    for name in ('libmidas.a', 'mfe.o')}
    libraries = link_libraries(midas)
    output.mkdir(parents=True, mode=0o755)
    # Command-line Make variables override Makefile defaults; no writes to dependency tree.
    command = ['make', '-C', str(repo / 'easiroc_frontend'),
        'MIDASSYS=' + str(midas.absolute()), 'BUILD_DIR=' + str(output / 'objects'),
        'BIN_DIR=' + str(output / 'easiroc_frontend/bin'),
        'BIN_TEST_DIR=' + str((output / 'tests').relative_to(repo / 'easiroc_frontend')), 'NODE=' + str(node.absolute()),
        'LDLIBS=' + shlex.join(libraries)]
    subprocess.run(command + ['feeasiroc', 'check', 'check-web'], check=True)
    for relative in deploy.FILES:
        if relative == deploy.BINARY:
            continue
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / relative, target)
    for name, sha in dependencies.items():
        deploy.require(deploy.digest(deploy.regular(midas / 'lib' / name)) == sha,
                       'MIDAS dependency changed during build')
    manifest = {'format': 1, 'files': {f: deploy.digest(deploy.regular(output / f)) for f in deploy.FILES},
                'midas': dependencies, 'link_libraries': libraries,
                'source_commit': subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()}
    deploy.atomic(output / 'manifest.json', deploy.encoded(manifest), 0o644)
    print(json.dumps(manifest, indent=2))
    print('Release:', output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--midas', type=Path, required=True)
    parser.add_argument('--node', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.output, args.midas, args.node)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Release preparation failed (no manifest means unusable release):', error, file=sys.stderr)
        sys.exit(1)
