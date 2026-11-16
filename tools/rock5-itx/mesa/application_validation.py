"""Check complete animated GLTeapot captures, lifecycle and native queue evidence.

Frames have structural checks and require a separate visual review. Animation
does not supply a deterministic software/native pixel oracle.
"""
import hashlib
from pathlib import Path
import re

from window_validation import png

MENU = [('FPS display', '0'), ('Perspective', '1'), ('Filled polygons', '0'),
    ('Filled polygons', '1'), ('Lighting', '0'), ('Lighting', '1'), ('Perspective', '0')]
COUNTERS = set(('clients buffers bytes vms generations vm_pages heaps chunks heap_bytes '
    'heap_pages heap_generations sync_clients sync_objects sync_points sync_events '
    'sync_exports sync_waits kernel_areas user_maps').split())


def native_evidence(body):
    properties = re.findall(r'^HAIKU_MESA_NATIVE_GPU handle=(\d+) id=([0-9a-f]+) '
        r'shader=([0-9a-f]+) firmware=([0-9a-f]+) csf=([0-9a-f]+) '
        r'registers=(\d+) reserved=(\d+)\s*$', body, re.M)
    assert len(properties) == body.count('HAIKU_MESA_NATIVE_GPU ') == 1
    handle, gpu, shader, firmware, csf, registers, reserved = properties[0]
    assert int(handle) > 0
    assert [int(v, 16) for v in (gpu, shader, firmware, csf)] == [
        0xa8670005, 0x50005, 0x01050000, 0x040a0412]
    assert (int(registers), int(reserved)) == (96, 4)
    errors = re.findall(r'^HAIKU_MESA_NATIVE_ERROR .*$', body, re.M)
    assert errors == ['HAIKU_MESA_NATIVE_ERROR op=4d435340 bytes=48 result=-1 errno=-2147459069']
    assert body.index(errors[0]) < body.index('HAIKU_MESA_NATIVE_GPU ')
    submitted = {}
    streams = 0
    submit_records = 0
    for row in re.findall(r'^HAIKU_MESA_NATIVE_SUBMIT handle=(\d+) generation=(\d+) '
            r'address=([0-9a-f]+) bytes=(\d+) waits=(\d+) signals=(\d+) '
            r'sequence=(\d+)\s*$', body, re.M):
        h, generation, address, size, waits, signals, sequence = row
        h, generation, size, waits, signals, sequence = map(int,
            (h, generation, size, waits, signals, sequence))
        address = int(address, 16)
        assert sequence == submitted.get(h, 0) + 1 and generation > 0
        assert (size == address == 0) or (size > 0 and size % 8 == 0
            and address >= 65536 and address + size < 1 << 47)
        assert waits + signals <= 64 and signals > 0
        submitted[h] = sequence
        streams += size > 0
        submit_records += 1
    queues = []
    seen = set()
    for row in re.findall(r'^HAIKU_MESA_NATIVE_QUEUE handle=(\d+) query=(-?\d+) '
            r'state=(\d+) error=(-?\d+) pending=(\d+) submitted=(\d+) '
            r'completed=(\d+) failed=(\d+)\s*$', body, re.M):
        h, query, state, error, pending, accepted, completed, failed = map(int, row)
        assert h not in seen and query == error == pending == failed == 0
        assert state in (1, 2) and accepted == completed == submitted.get(h, 0)
        queues.append(dict(handle=h, submitted=accepted, completed=completed))
        seen.add(h)
    assert set(submitted) <= seen and len(queues) == 2 and streams >= 8
    assert body.count('HAIKU_MESA_NATIVE_SUBMIT ') == submit_records
    assert body.count('HAIKU_MESA_NATIVE_QUEUE ') == len(queues)
    assert int(handle) in seen and submitted.get(int(handle), 0) == 0
    snapshots = []
    for row in re.findall(r'^ROCK5_MESA_NATIVE_LIFETIME (.*)$', body, re.M):
        values = [field.split('=') for field in row.split()]
        assert len(values) == len({k for k, v in values})
        snapshots.append({k: int(v) for k, v in values})
    assert [s.pop('cycle') for s in snapshots] == [99, 0]
    assert snapshots[0] == snapshots[1] and set(snapshots[0]) == COUNTERS
    assert snapshots[0]['clients'] == snapshots[0]['sync_clients'] == 1
    assert all(v == 0 for k, v in snapshots[0].items() if k not in ('clients', 'sync_clients'))
    assert not re.search(r'Haiku CSF .* failed', body)
    return dict(gpu=properties[0], queues=queues, submissions=sum(submitted.values()),
        completions=sum(q['completed'] for q in queues), streams=streams,
        allocation_baseline=snapshots[0], expected_runtime_joins=1)


def validate(text, output=None, software=False, mode=None):
    for marker in ('ROCK5_APPLICATION_FAILURE', 'ROCK5_MESA_FAILURE', 'PANIC:',
            'Kernel Debugging Land', 'DEBUGGER:', 'mutex->owner', 'No EGL renderer'):
        assert marker not in text
    if mode is None:
        mode = '--software' if software else '--native'
    assert re.findall(r'^ROCK5_APPLICATION_SELF_TEST_PASS accepted=4 rejected=7 cleanup=11$',
        text, re.M) == ['ROCK5_APPLICATION_SELF_TEST_PASS accepted=4 rejected=7 cleanup=11']
    assert text.count('ROCK5_APPLICATION_SELF_TEST_PASS ') == 1
    assert text.index('ROCK5_APPLICATION_SELF_TEST_PASS ') < text.index('ROCK5_APPLICATION_RUN_BEGIN ')
    assert re.findall(r'^ROCK5_APPLICATION_PASS processes=2 frames=16$', text, re.M) == [
        'ROCK5_APPLICATION_PASS processes=2 frames=16']
    assert text.count('ROCK5_APPLICATION_RUN_BEGIN ') == 2
    assert text.count('ROCK5_APPLICATION_RUN_EXIT ') == 2
    runs = re.findall(r'^ROCK5_APPLICATION_RUN_BEGIN cycle=(\d+) mode=(\S+)\n(.*?)'
        r'^ROCK5_APPLICATION_RUN_EXIT cycle=(\d+) application=0 frames=0\s*$', text, re.M | re.S)
    assert [(int(c), m, int(end)) for c, m, body, end in runs] == [(0, mode, 0), (1, mode, 1)]
    for marker, count in dict(STARTED=2, MENU=14, CAPTURE=16, ANIMATION=8,
            CONTROLLER_PASS=2, PIXELS_BEGIN=16, PIXELS_END=16, FRAMES_PASS=2, PASS=1).items():
        assert text.count('ROCK5_APPLICATION_' + marker + ' ') == count
    for marker in ('HAIKU_MESA_NATIVE_', 'ROCK5_MESA_NATIVE_LIFETIME '):
        assert text.count(marker) == sum(body.count(marker) for _, _, body, _ in runs)
        if software:
            assert marker not in text
    frames = []
    cycles = []
    teams = []
    for cycle_string, _, body, _ in runs:
        cycle = int(cycle_string)
        started = re.findall(r'^ROCK5_APPLICATION_STARTED team=(\d+) '
            r'signature=application/x-vnd.vos-GLTeapot$', body, re.M)
        assert len(started) == 1 and int(started[0]) > 0
        teams.append(int(started[0]))
        assert re.findall(r'^ROCK5_APPLICATION_CONTROLLER_PASS team=(\d+) '
            r'captures=8 normal_exit=0$', body, re.M) == started
        assert re.findall(r'^ROCK5_APPLICATION_MENU item=(.*?) marked=([01])$', body, re.M) == MENU
        capture = re.findall(r'^ROCK5_APPLICATION_CAPTURE phase=(\d+) sample=(\d+) '
            r'width=(\d+) height=(\d+) coloured=(\d+) dark=(\d+) chromatic=(\d+) '
            r'white=(\d+) yellow=(\d+) file=(\S+)$', body, re.M)
        blocks = re.findall(r'^ROCK5_APPLICATION_PIXELS_BEGIN phase=(\d+) sample=(\d+) '
            r'width=(\d+) height=(\d+) format=RGB8 origin=upper-left\n(.*?)'
            r'^ROCK5_APPLICATION_PIXELS_END phase=(\d+) sample=(\d+) pixels=(\d+)\s*$',
            body, re.M | re.S)
        assert len(capture) == len(blocks) == 8
        assert body.count('ROCK5_APPLICATION_PIXELS_BEGIN ') == body.count('ROCK5_APPLICATION_PIXELS_END ') == 8
        pictures = {}
        geometry = {}
        pixel_total = 0
        for record, header, (phase, sample) in zip(blocks, capture,
                [(p, s) for p in range(4) for s in range(2)]):
            p, s, w, h, rows, ep, es, pixels = record
            width, height = int(w), int(h)
            assert (int(p), int(s)) == (phase, sample) == (int(ep), int(es))
            assert tuple(map(int, header[:4])) == (phase, sample, width, height)
            assert width == (320 if phase == 3 else 256) and 128 <= height <= 512
            assert int(pixels) == width * height
            lines = rows.splitlines()
            assert len(lines) == height
            rgb = bytearray()
            for y, line in enumerate(lines):
                assert re.fullmatch(r'row=' + f'{y:03d}' + r' [0-9a-f]{' + str(width * 6) + '}', line)
                rgb.extend(bytes.fromhex(line.split(' ', 1)[1]))
            coloured = dark = chromatic = white = yellow = 0
            for i in range(0, len(rgb), 3):
                pixel = rgb[i:i + 3]
                black = max(pixel) <= 8
                dark += black
                coloured += not black
                chromatic += max(pixel) - min(pixel) > 8
                white += min(pixel) >= 240
                yellow += pixel[0] >= 240 and pixel[1] >= 240 and pixel[2] <= 8
            assert tuple(map(int, header[4:9])) == (coloured, dark, chromatic, white, yellow)
            assert header[9] == f'/boot/home/rock5-sustained-ram/application-{cycle}/phase{phase}-sample{sample}.ppm'
            assert coloured >= 256 and dark >= width * height // 8
            if phase == 3:
                assert (white == coloured and chromatic == 0) or (yellow == coloured and chromatic == coloured)
            else:
                assert chromatic >= 128
            pictures[phase, sample] = bytes(rgb)
            geometry[phase, sample] = (width, height)
            item = dict(cycle=cycle, phase=phase, sample=sample, width=width, height=height,
                pixels=width * height, sha256=hashlib.sha256(rgb).hexdigest(),
                foreground=coloured, dark=dark, chromatic=chromatic, white=white, yellow=yellow)
            if output:
                directory = Path(output)
                directory.mkdir(parents=True, exist_ok=True)
                name = f'cycle{cycle}-phase{phase}-sample{sample}'
                (directory / (name + '.rgb')).write_bytes(rgb)
                png(directory / (name + '.png'), rgb, width, height)
                item['png'] = str(directory / (name + '.png'))
            frames.append(item)
            pixel_total += width * height
        animation = re.findall(r'^ROCK5_APPLICATION_ANIMATION phase=(\d+) changed_pixels=(\d+)$', body, re.M)
        assert [int(p) for p, n in animation] == list(range(4))
        for phase, (_, count) in enumerate(animation):
            before, after = pictures[phase, 0], pictures[phase, 1]
            assert geometry[phase, 0] == geometry[phase, 1]
            changed = sum(before[i:i+3] != after[i:i+3] for i in range(0, len(before), 3))
            assert changed == int(count) and changed >= 128
        assert len({geometry[p, s] for p in range(3) for s in range(2)}) == 1
        assert geometry[3, 0][1] - geometry[0, 0][1] == 32
        assert re.findall(r'^ROCK5_APPLICATION_FRAMES_PASS frames=8 pixels=(\d+)$', body, re.M) == [str(pixel_total)]
        if software:
            assert 'HAIKU_MESA_NATIVE_' not in body and 'ROCK5_MESA_NATIVE_LIFETIME' not in body
            cycles.append(dict(software=True, team=teams[-1]))
        elif mode == '--system':
            # The system launch sets no HAIKU_CSF_TRACE, so the renderer prints no GPU
            # trace; GPU use is established by the UART interval, not by this transcript.
            cycles.append(dict(team=teams[-1], launch='system',
                traced='HAIKU_MESA_NATIVE_GPU ' in body))
        else:
            cycles.append(dict(team=teams[-1], **native_evidence(body)))
    assert len(set(teams)) == 2
    return dict(status='pass', software=software, processes=2, frames=frames,
        pixels=sum(f['pixels'] for f in frames), cycles=cycles,
        native_submissions=sum(c.get('submissions', 0) for c in cycles),
        native_completions=sum(c.get('completions', 0) for c in cycles),
        visual_review_required=True, exact_software_native_pixel_identity=False)
