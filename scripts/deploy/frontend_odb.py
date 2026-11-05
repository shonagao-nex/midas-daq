"""Additive frontend schema planning. No development ODB or hardware access.

Defaults follow easiroc_run_settings.h's visitor/default structs and fehul.cxx.
HV settings and HUL settings remain owned by their frontend initializers.
"""
import ipaddress
import math

EASI = '/Equipment/EASIROC'
HUL = '/Equipment/HUL Scaler 1'
BOOL, INT, DWORD, DOUBLE, STRING, KEY, QWORD = 8, 7, 6, 10, 12, 15, 18


def number(value):
    return int(value, 16 if value.startswith('0x') else 10) if isinstance(value, str) else value


def extras(prefix):
    result = {}
    for chip in ('ASIC1', 'ASIC2'):
        for name, tid, default in (('Enable', BOOL, False), ('Channel', INT, 0), ('Signal', INT, 4)):
            result[f'{prefix}/Probe/{chip}/{name}'] = (tid, 1, 0, default)
    for name, default in (('Pattern', 3), ('OneChannel', 0), ('HitNumThreshold', 1)):
        result[f'{prefix}/SelectableLogic/{name}'] = (INT, 1, 0, default)
    result[prefix + '/SelectableLogic/AndChannels'] = (BOOL, 64, 0, [False] * 64)
    result[prefix + '/TriggerWidth'] = (INT, 1, 0, 118)
    return result


def easiroc_settings():
    prefix = EASI + '/Settings'
    result = {prefix + '/Enabled': (BOOL, 1, 0, True),
        prefix + '/Network/IPAddress': (STRING, 1, 64, '192.168.10.40'),
        prefix + '/ASICSlowControl/ApplyAtBOR': (BOOL, 1, 0, False)}
    for name, default in (('ADCEnabled', True), ('TDCEnabled', True), ('ScalerEnabled', False)):
        result[prefix + '/Acquisition/' + name] = (BOOL, 1, 0, default)
    for chip in ('ASIC1', 'ASIC2'):
        for name, default in (('DiscriminatorDACCode', 600), ('DiscriminatorDACSlope', 1),
                ('HGFeedbackCapacitance', 100), ('LGFeedbackCapacitance', 100),
                ('HGShapingTime', 100), ('LGShapingTime', 50)):
            result[f'{prefix}/{chip}/{name}'] = (INT, 1, 0, default)
        result[f'{prefix}/{chip}/InputDAC'] = (INT, 32, 0, [480] * 32)
        result[f'{prefix}/{chip}/ChannelEnabled'] = (BOOL, 32, 0, [True] * 32)
    result.update(extras(prefix))
    return result


def frontend_owned():
    # Validate when present; never create these defaults from deploy3.
    result = {EASI + '/Settings/HV/CurrentLimit_uA': (DOUBLE, 1, 0, 20.),
              EASI + '/Settings/HV/HardwareAccessEnabled': (BOOL, 1, 0, False)}
    # Mailboxes/status remain frontend-owned, but incompatible existing schema must be
    # detected before any additive Settings mutation or frontend restart.
    for path, tid in ((EASI + '/Commands/ASICSlowControl/ApplyRequestId', DWORD),
                      (EASI + '/Commands/BufferClearRequestId', DWORD)):
        result[path] = (tid, 1, 0, None)
    for group, fields in {
            'ASICSlowControl': {'ActiveRequestId': DWORD, 'LastHandledRequestId': DWORD,
                'LastSuccessfulRequestId': DWORD, 'ApplyState': STRING, 'ApplyInProgress': BOOL,
                'LastAttemptSucceeded': BOOL, 'LastApplyError': STRING, 'LastApplyUnixTime': QWORD},
            'BufferClear': {'ActiveRequestId': DWORD, 'LastHandledRequestId': DWORD,
                'LastSuccessfulRequestId': DWORD, 'State': STRING, 'InProgress': BOOL,
                'LastAttemptSucceeded': BOOL, 'LastError': STRING, 'LastClearUnixTime': QWORD,
                'NIMEASIROCResult': STRING, 'DrainedBytes': QWORD}}.items():
        for name, tid in fields.items():
            result[f'{EASI}/Variables/{group}/{name}'] = (tid, 1, 1 if tid == STRING else 0, None)
    for name, tid in {'RequestId': DWORD, 'RequestPayload': STRING, 'Operation': STRING,
                      'TargetVoltage_V': DOUBLE, 'Enable': BOOL}.items():
        result[f'{EASI}/Commands/HV/{name}'] = (tid, 1, 1 if tid == STRING else 0, None)
    groups = {
        BOOL: ('MeasurementValid', 'OperationsAvailable', 'EnableAvailable', 'VoltageAvailable',
               'StopAvailable', 'EmergencyAvailable', 'ProtectionActive', 'InProgress'),
        STRING: ('EnabledState', 'BackendMode', 'StateSource', 'SetVoltageState', 'RampStatus',
                 'Result', 'Error', 'MonitoringStatus', 'BlockedReason'),
        DOUBLE: ('SetVoltage_V', 'TargetVoltage_V', 'MeasuredVoltage_V', 'MeasuredCurrent_uA'),
        QWORD: ('HeartbeatUnixTime', 'LastUpdateUnixTime', 'LastUpdateUnixTimeMs', 'ResultUnixTime'),
        DWORD: ('LastHandledRequestId', 'ActiveRequestId', 'ResultRequestId')}
    for tid, names in groups.items():
        for name in names:
            result[f'{EASI}/Variables/HV/{name}'] = (tid, 1, 1 if tid == STRING else 0, None)
    for name, tid, size, default in (
            ('IPAddress', STRING, 64, '192.168.10.60'), ('RBCPPort', DWORD, 0, 4660),
            ('TCPPort', DWORD, 0, 24), ('EnableBlock', DWORD, 0, 3),
            ('FreshnessMs', DWORD, 0, 2000), ('DrainMs', DWORD, 0, 2000)):
        result[HUL + '/Settings/' + name] = (tid, 1, size, default)
    for name, count in (('HCUM', 64), ('HDIF', 64), ('HVAL', 15)):
        result[HUL + '/Variables/' + name] = (DWORD, count, 0, None)
    for name in ('Connection', 'Gate', 'LastError'):
        result[HUL + '/Status/' + name] = (STRING, 1, 1, None)
    result[HUL + '/Status/CommCheckedAt'] = (DWORD, 1, 0, None)
    return result


def protected_history():
    base = EASI + '/Variables/ASICSlowControl'
    prefix = base + '/LastApplied'
    result = {prefix + '/Valid': (BOOL, 1, 0, None), prefix + '/RequestId': (DWORD, 1, 0, None),
              prefix + '/ApplyUnixTime': (QWORD, 1, 0, None), prefix + '/Extended': (BOOL, 1, 0, None),
              base + '/HardwareStateIndeterminate': (BOOL, 1, 0, None)}
    for path, spec in easiroc_settings().items():
        suffix = path[len(EASI + '/Settings'):]
        if suffix.startswith(('/ASIC1/', '/ASIC2/')):
            result[prefix + suffix] = (*spec[:3], None)
    result.update({p: (*spec[:3], None) for p, spec in extras(prefix).items()})
    return result


def validate(rpc, path, spec, required=False):
    tid, count, minimum, _ = spec
    parent = path.rsplit('/', 1)[0]
    while parent not in ('', '/'):
        ancestor = rpc.key(parent)
        if ancestor is not None and ancestor['type'] != KEY:
            raise RuntimeError('Frontend parent is not a directory: ' + parent)
        parent = parent.rsplit('/', 1)[0]
    key = rpc.key(path)
    if key is None:
        if required:
            raise RuntimeError('Missing protected frontend key: ' + path)
        return False
    if (key['type'] != tid or key['num_values'] != count or
            (tid == STRING and key['item_size'] < minimum)):
        raise RuntimeError(f'Frontend schema {path}: expected type={tid} count={count} string>={minimum}, actual {key}')
    return True


class _MetadataView:
    def __init__(self, rpc):
        self.rpc, self.cache = rpc, {}

    def key(self, path):
        if path not in self.cache:
            self.cache[path] = self.rpc.key(path)
        return self.cache[path]

    def __getattr__(self, name):
        return getattr(self.rpc, name)


def plan(rpc):
    rpc = _MetadataView(rpc)
    additions, preserved, deferred = [], {}, []
    settings = easiroc_settings()
    for path, spec in settings.items():
        if validate(rpc, path, spec):
            preserved[path] = rpc.value(path)
        else:
            additions.append({'path': path, 'type': spec[0], 'count': spec[1],
                              'string_length': spec[2], 'value': spec[3]})
    for path, spec in frontend_owned().items():
        if validate(rpc, path, spec):
            preserved[path] = rpc.value(path)
        else:
            deferred.append(path)
    for path, spec in protected_history().items():
        if validate(rpc, path, spec):
            preserved[path] = rpc.value(path)
    hist = EASI + '/Variables/ASICSlowControl/LastApplied'
    if rpc.value(hist + '/Valid', missing_ok=True) is True:
        for path, spec in protected_history().items():
            if path.startswith(hist + '/') and '/Extended' not in path and path not in extras(hist):
                validate(rpc, path, spec, required=True)
    if rpc.value(hist + '/Extended', missing_ok=True) is True:
        for path, spec in extras(hist).items():
            validate(rpc, path, spec, required=True)
    # Only Enabled is seeded, before any possible first HUL startup. MFE record merging
    # retains this existing BOOL while completing Common (odb.cxx db_create_record).
    enabled = HUL + '/Common/Enabled'
    if validate(rpc, enabled, (BOOL, 1, 0, False)):
        preserved[enabled] = rpc.value(enabled)
    else:
        additions.append({'path': enabled, 'type': BOOL, 'count': 1, 'string_length': 0, 'value': False})
    for name, spec in {'Event ID': (4, 1, 0, None), 'Period': (INT, 1, 0, None),
                       'Buffer': (STRING, 1, 1, None), 'Log history': (INT, 1, 0, None)}.items():
        path = HUL + '/Common/' + name
        if validate(rpc, path, spec):
            preserved[path] = rpc.value(path)
    if HUL + '/Common/Event ID' in preserved and number(preserved[HUL + '/Common/Event ID']) != 10:
        raise RuntimeError('Existing HUL Event ID differs from 10; not overwritten')
    if HUL + '/Common/Buffer' in preserved and preserved[HUL + '/Common/Buffer'] != 'SYSTEM':
        raise RuntimeError('Existing HUL buffer differs from SYSTEM; not overwritten')
    if HUL + '/Common/Period' in preserved and number(preserved[HUL + '/Common/Period']) <= 0:
        raise RuntimeError('Invalid existing HUL period')
    if rpc.key('/Equipment') is not None:
        def child(record, name):
            return next((v for k, v in record.items() if k.casefold() == name.casefold()), None)
        for name, record in rpc.copy('/Equipment').items():
            if '/' in name or not isinstance(record, dict) or name.casefold() == 'hul scaler 1':
                continue
            common = child(record, 'Common')
            event_id = child(common, 'Event ID') if isinstance(common, dict) else None
            if event_id is not None and number(event_id) in (10, 11):
                raise RuntimeError('HUL Event ID 10/11 already used by ' + name)
    for path, value in preserved.items():
        if path.endswith('/IPAddress'):
            ipaddress.IPv4Address(value)
        if path.endswith('/CurrentLimit_uA') and not (math.isfinite(value) and 0 < value < 0.0364 * 65535):
            raise RuntimeError('Invalid CurrentLimit: ' + path)
        if path.startswith(HUL + '/Settings/'):
            name = path.rsplit('/', 1)[-1]
            n = number(value) if name != 'IPAddress' else None
            if ((name in ('RBCPPort', 'TCPPort') and not 0 < n <= 65535) or
                    (name == 'EnableBlock' and n != 3) or (name == 'FreshnessMs' and n <= 0) or
                    (name == 'DrainMs' and not 200 <= n <= 10000)):
                raise RuntimeError('Invalid HUL setting: ' + path)
    easi_ip = preserved.get(EASI + '/Settings/Network/IPAddress', '192.168.10.40')
    hul_ip = preserved.get(HUL + '/Settings/IPAddress', '192.168.10.60')
    if easi_ip == hul_ip:
        raise RuntimeError('EASIROC and HUL have the same configured IP; review without overwriting either')
    return {'add': additions, 'preserved': preserved, 'frontend_initializes': deferred,
            'desired_later': {'CurrentLimit_uA': 20, 'InputDAC': 480},
            'hul_first_start': 'not started by deployment; missing Common/Enabled is staged false'}


def apply(rpc, expected, require_stopped, checkpoint=None):
    # Replan before the first mutation. No history/Commands/status values are written here.
    if not expected['add']:
        return
    if plan(rpc) != expected:
        raise RuntimeError('Frontend ODB changed after preflight; inspect the new plan')
    for item in expected['add']:
        path = item['path']
        require_stopped(rpc)
        if rpc.key(path) is not None:
            raise RuntimeError('ODB key appeared during deployment: ' + path)
        args = {'path': path, 'type': item['type'], 'array_length': item['count']}
        if item['string_length']:
            args['string_length'] = item['string_length']
        if checkpoint:
            checkpoint(item, 'intent')
        rpc.checked('db_create', [args])
        require_stopped(rpc)
        rpc.checked('db_paste', {'paths': [path], 'values': [item['value']]})
        validate(rpc, path, (item['type'], item['count'], item['string_length'], None), required=True)
        if rpc.value(path) != item['value']:
            raise RuntimeError('Frontend initial value readback failed: ' + path)
        if checkpoint:
            checkpoint(item, 'written')
    for path, value in expected['preserved'].items():
        if rpc.value(path) != value:
            raise RuntimeError('Existing frontend value changed: ' + path)
