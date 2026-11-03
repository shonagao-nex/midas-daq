#!/usr/bin/env python3
"""Read-only MIDAS HUL ID 10/11 inspection; supports standard LZ4 frames via liblz4."""
import argparse
import collections
import ctypes as C
import ctypes.util
import hashlib
import json
import pathlib
import struct


def load(path):
    raw = path.read_bytes()
    if path.suffix != '.lz4':
        return raw
    lib = C.CDLL(ctypes.util.find_library('lz4'))
    lib.LZ4F_createDecompressionContext.argtypes = [C.POINTER(C.c_void_p), C.c_uint]
    lib.LZ4F_createDecompressionContext.restype = C.c_size_t
    lib.LZ4F_decompress.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_size_t), C.c_void_p, C.POINTER(C.c_size_t), C.c_void_p]
    lib.LZ4F_decompress.restype = C.c_size_t
    lib.LZ4F_isError.argtypes = [C.c_size_t]
    lib.LZ4F_isError.restype = C.c_uint
    lib.LZ4F_freeDecompressionContext.argtypes = [C.c_void_p]
    ctx = C.c_void_p()
    assert not lib.LZ4F_isError(lib.LZ4F_createDecompressionContext(C.byref(ctx), 100))
    src = C.create_string_buffer(raw)
    dst = C.create_string_buffer(1024 * 1024)
    offset, total, chunks, hint = 0, 0, [], 1
    try:
        while offset < len(raw) or hint:
            used, size = C.c_size_t(len(raw)-offset), C.c_size_t(len(dst))
            hint = lib.LZ4F_decompress(ctx, dst, C.byref(size), C.byref(src, offset), C.byref(used), None)
            if lib.LZ4F_isError(hint):
                raise ValueError('LZ4 decompression error')
            if not used.value and not size.value:
                raise ValueError('Truncated LZ4 frame')
            offset += used.value
            total += size.value
            if total > 512 * 1024 * 1024:
                raise ValueError('Decompressed file exceeds 512 MiB analysis limit')
            chunks.append(dst.raw[:size.value])
    finally:
        lib.LZ4F_freeDecompressionContext(ctx)
    return b''.join(chunks)


def analyze(path):
    blob = load(path)
    offset, counts, records = 0, collections.Counter(), []
    while offset < len(blob):
        if offset+16 > len(blob):
            raise ValueError('Truncated MIDAS header')
        eid, mask, serial, stamp, size = struct.unpack_from('<HHIII', blob, offset)
        start, end = offset+16, offset+16+size
        if end > len(blob):
            raise ValueError('Truncated MIDAS event')
        counts[eid] += 1
        if eid in (10, 11):
            bank_size, flags = struct.unpack_from('<II', blob, start)
            assert bank_size+8 == size, (bank_size, size)
            assert flags & 0x10, 'HUL requires 32-bit bank headers'
            pos, banks, types = start+8, {}, {}
            while pos < end:
                name, tid, nbytes = struct.unpack_from('<4sII', blob, pos)
                name = name.decode('ascii')
                pos += 16 if flags & 0x20 else 12
                assert pos+nbytes <= end and nbytes % 4 == 0
                assert name not in banks
                banks[name] = list(struct.unpack_from('<'+'I'*(nbytes//4), blob, pos))
                types[name] = tid
                pos += (nbytes+7) & ~7
            assert pos == end
            assert set(banks) == {'HCUM', 'HDIF', 'HVAL'}
            assert [len(banks[k]) for k in ('HCUM','HDIF','HVAL')] == [64,64,15]
            assert all(v == 6 for v in types.values()), types
            v = banks['HVAL']
            records.append({'id':eid, 'serial':serial, 'timestamp':stamp,
                'run':v[0], 'final':v[1], 'valid':v[2], 'fresh':v[3], 'fault':v[4],
                'valid_mask':v[5] | v[6]<<32, 'age_ms':v[7], 'self':v[8],
                'accepted':v[11] | v[12]<<32, 'missing':v[13] | v[14]<<32,
                **banks})
        offset = end
    finals = [r for r in records if r['id']==11]
    periodic = [r for r in records if r['id']==10]
    increasing = []
    for ch in range(64):
        values = [r['HCUM'][ch] for r in records if r['valid']]
        if values and max(values)>min(values):
            increasing.append({'ch':ch, 'connector':('U' if ch<32 else 'D'), 'local_ch':ch%32,
                'first':values[0], 'last':values[-1],
                'valid_deltas':[r['HDIF'][ch] for r in records if r['valid_mask'] & (1<<ch)]})
    return {'file':str(path), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'decompressed_bytes':len(blob), 'event_counts':dict(counts),
        'periodic_count':len(periodic), 'final_count':len(finals),
        'increasing_channels':increasing,
        'checks':{
            'one_final_event':len(finals)==1,
            'consistent_run_number':bool(records) and len({r['run'] for r in records})==1,
            'correct_final_flags':all(r['final']==(1 if r['id']==11 else 0) for r in records),
            'cumulative_valid_records':sum(bool(r['valid']) for r in records),
            'fresh_records':sum(bool(r['fresh']) for r in records),
            'fault_records':sum(bool(r['fault']) for r in records),
            'last_accepted_events':finals[-1]['accepted'] if finals else None,
            'last_self_counter':finals[-1]['self'] if finals else None,
            'target_channels':{str(ch):[r['HCUM'][ch] for r in records] for ch in (0,1,32,33)}},
        'records':records}

if __name__ == '__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('file',type=pathlib.Path)
    args=ap.parse_args()
    print(json.dumps(analyze(args.file),indent=2))
