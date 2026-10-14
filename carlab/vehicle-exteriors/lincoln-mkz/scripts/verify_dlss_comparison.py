#!/usr/bin/env python3
"""Verify actual inference artifacts and publish the MKZ gallery catalog."""
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DLSS = ROOT / 'dlss'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


inputs = json.loads((DLSS / 'inputs.json').read_text())
assert digest(Path(inputs['native_report'])) == inputs['native_report_sha256']
expected = {entry['camera']: entry for entry in inputs['frames']}
assert len(expected) == 8
catalog = dict(profiles={})
verification = dict(status='PASS', backend='actual CUDA community DLSS 5 approximation',
                    source_count=8, enhanced_count=16, resolution=[1280, 800],
                    unchanged_native_report=True, profiles={}, checks=[])
for profile in ('standard', 'natural'):
    report = json.loads((DLSS / profile / 'report.json').read_text())
    assert report['status'] == 'complete' and report['frame_count'] == 8
    assert report['profile'] == profile and report['intensity'] == 1
    assert report['temporal'] is False and '2080 Ti' in report['gpu']
    assert report['source_manifest_sha256'] == digest(DLSS / 'inputs.json')
    assert {f['source']['camera'] for f in report['frames']} == set(expected)
    entries = {}
    for result in report['frames']:
        entry = result['source']
        assert entry == expected[entry['camera']]
        source = Path(entry['path'])
        original = DLSS / profile / result['original']
        enhanced = DLSS / profile / result['enhanced']
        assert digest(source) == entry['sha256']
        assert digest(original) == result['original_sha256']
        assert digest(enhanced) == result['enhanced_sha256']
        with Image.open(source) as image:
            source_rgb = np.asarray(image.convert('RGB'))
        with Image.open(original) as image:
            original_rgb = np.asarray(image.convert('RGB'))
        with Image.open(enhanced) as image:
            enhanced_rgb = np.asarray(image.convert('RGB'))
        assert source_rgb.shape == original_rgb.shape == enhanced_rgb.shape == (800, 1280, 3)
        assert np.array_equal(source_rgb, original_rgb)
        assert not np.array_equal(source_rgb, enhanced_rgb)
        entries[entry['camera']] = dict(
            native=str(source.relative_to(ROOT)),
            enhanced=str(enhanced.relative_to(ROOT)),
            frame=entry['frame'], timestamp=entry['timestamp'],
            seconds=result['end_to_end_seconds'],
        )
        verification['checks'].append(dict(profile=profile, camera=entry['camera'],
                                           frame=entry['frame'], native_hash_unchanged=True,
                                           original_pixels_exact=True, output_shape_preserved=True,
                                           enhanced_hash_verified=True))
    catalog['profiles'][profile] = dict(frames=entries)
    verification['profiles'][profile] = {
        key: report[key] for key in ('model_load_seconds', 'first_frame_seconds',
                                    'warmed_median_seconds', 'weights_sha256',
                                    'source_revisions', 'gpu', 'gpu_uuid',
                                    'peak_cuda_allocated_mib')
    }
assert verification['profiles']['standard']['weights_sha256'] == verification['profiles']['natural']['weights_sha256']
(DLSS / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
(DLSS / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
print(json.dumps({key: value for key, value in verification.items() if key != 'checks'}, indent=2))
