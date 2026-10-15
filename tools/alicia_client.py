# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

#!/usr/bin/env python3
"""
Story of Alicia Protocol Client
Logs into Story of Alicia and retrieves server info.

Protocol reverse-engineered from:
  https://github.com/Story-Of-Alicia/alicia-server

Usage:
  python3 alicia_client.py [--skip-tests]
"""

import struct
import socket
import sys
from urllib.parse import urlparse, parse_qs

# ── Protocol Constants ──────────────────────────────────────

BUFFER_SIZE = 4096
XOR_CONTROL = 0xA20191CB
XOR_MULTIPLIER = 0x20080825

CMD_LOGIN = 0x7
CMD_LOGIN_OK = 0x8
CMD_LOGIN_CANCEL = 0x9
CMD_HEARTBEAT = 0x12

SERVERS = {
    'soa': {
        'name': 'Story of Alicia',
        'host': '5.75.155.237',
        'port': 10030,
    },
}

SOA_OAUTH_URL = (
    "https://discord.com/oauth2/authorize"
    "?client_id=1272602862043795586"
    "&response_type=code"
    "&redirect_uri=https%3A%2F%2Fauthentication.storyofalicia.com%2F"
    "&scope=identify"
)

LOGIN_CANCEL_REASONS = {
    0: "Generic",
    1: "InvalidUser (token lookup failed)",
    2: "Duplicated (already logged in)",
    3: "InvalidVersion",
    4: "InvalidEquipment",
    5: "InvalidLoginId (empty fields)",
    6: "DisconnectYourself",
}

GENDERS = {0: "Unspecified", 1: "Boy", 2: "Girl"}
ROLES = {0: "User", 1: "PowerUser", 2: "GameMaster"}
HORSE_TYPES = {0: "Adult", 1: "Foal", 2: "Stallion", 3: "Rented"}
INJURIES = {
    0: "None", 17: "Minor Muscle Strain", 18: "Severe Muscle Strain",
    33: "Minor Wounds", 34: "Severe Wounds",
    65: "Minor Fracture", 66: "Severe Fracture",
}

# ── Protocol Core ───────────────────────────────────────────

def decode_message_magic(value: int) -> tuple[int, int]:
    value &= 0xFFFFFFFF
    length = 0
    if value & (1 << 15):
        section = value & 0x3FFF
        length = ((value & 0xFF) << 4) | ((section >> 8) & 0xF) | (section & 0xF000)
    first = value & 0xFFFF
    second = (value >> 16) & 0xFFFF
    xor_result = first ^ second
    cmd_id = (~(xor_result & 0xC000)) & xor_result & 0xFFFF
    return cmd_id, length


def encode_message_magic(cmd_id: int, length: int) -> int:
    cmd_id &= 0xFFFF
    combined = ((BUFFER_SIZE << 16) | length) & 0xFFFFFFFF
    enc = ((combined & 0x3FFF) | (combined << 14)) & 0xFFFF
    enc = ((((enc & 0xF) | 0xFF80) << 8) | ((combined >> 4) & 0xFF) | (enc & 0xF000)) & 0xFFFF
    enc = (enc | (((enc ^ cmd_id) & 0xFFFF) << 16)) & 0xFFFFFFFF
    return enc


def roll_code(code: int) -> int:
    code = (code * XOR_MULTIPLIER) & 0xFFFFFFFF
    code_s = code - 0x100000000 if code >= 0x80000000 else code
    ctrl_s = XOR_CONTROL - 0x100000000
    return (ctrl_s - code_s) & 0xFFFFFFFF


def xor_bytes(key: bytes, data: bytes) -> bytes:
    return bytes(b ^ key[i % 4] for i, b in enumerate(data))


def u32_bytes(val: int) -> bytes:
    return struct.pack('<I', val & 0xFFFFFFFF)


# ── Stream Reader ───────────────────────────────────────────

class StreamReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def remaining(self) -> int:
        return len(self.data) - self.pos

    def u8(self) -> int:
        v = self.data[self.pos]; self.pos += 1; return v

    def u16(self) -> int:
        v = struct.unpack_from('<H', self.data, self.pos)[0]; self.pos += 2; return v

    def i32(self) -> int:
        v = struct.unpack_from('<i', self.data, self.pos)[0]; self.pos += 4; return v

    def u32(self) -> int:
        v = struct.unpack_from('<I', self.data, self.pos)[0]; self.pos += 4; return v

    def u64(self) -> int:
        v = struct.unpack_from('<Q', self.data, self.pos)[0]; self.pos += 8; return v

    def string(self) -> str:
        end = self.data.index(0, self.pos)
        raw = self.data[self.pos:end]
        self.pos = end + 1
        for enc in ('euc-kr', 'utf-8', 'latin-1'):
            try:
                return raw.decode(enc)
            except (UnicodeDecodeError, ValueError):
                continue
        return raw.decode('latin-1', errors='replace')


# ── Packet Building ─────────────────────────────────────────

def build_login_packet(login_id: str, auth_key: str, member_no: int = 0) -> bytes:
    payload = bytearray()
    payload += struct.pack('<HH', 50, 281)
    payload += login_id.encode('utf-8') + b'\x00'
    payload += struct.pack('<I', member_no)
    payload += auth_key.encode('utf-8') + b'\x00'
    payload += struct.pack('<B', 0)

    code = roll_code(0)
    key = u32_bytes(code)
    padding = code & 7
    encrypted = xor_bytes(key, bytes(payload)) + b'\x00' * padding
    total = 4 + len(encrypted)
    magic = encode_message_magic(CMD_LOGIN, total)
    return struct.pack('<I', magic) + encrypted


# ── Full LoginOK Parser ────────────────────────────────────

def parse_login_ok(data: bytes) -> dict:
    r = StreamReader(data)
    info = {}

    try:
        # Header
        info['lobby_time_low'] = r.u32()
        info['lobby_time_high'] = r.u32()
        info['member0'] = r.u32()

        # Profile
        info['uid'] = r.u32()
        info['name'] = r.string()
        info['notice'] = r.string()
        info['gender'] = GENDERS.get(r.u8(), 'Unknown')
        info['introduction'] = r.string()

        # Equipment
        eq_count = r.u8()
        items = []
        for _ in range(eq_count):
            items.append({'uid': r.u32(), 'tid': r.u32(), 'expires': r.u32(), 'count': r.u32()})
        info['equipment'] = items

        # Expired items
        exp_count = r.u8()
        expired = []
        for _ in range(exp_count):
            expired.append({'uid': r.u32(), 'tid': r.u32(), 'expires': r.u32(), 'count': r.u32()})
        info['expired_items'] = expired

        # Level & economy
        info['level'] = r.u16()
        info['carrots'] = r.i32()
        info['level_progress'] = r.u32()
        info['role'] = ROLES.get(r.u32(), 'Unknown')
        info['val3'] = r.u8()

        # Settings
        settings_bits = r.u32()
        settings = {'type_bits': settings_bits}
        if settings_bits & (1 << 0):
            kb_count = r.u8()
            kb = []
            for _ in range(kb_count):
                kb.append({'type': r.u8(), 'unused': r.u8(), 'primary': r.u8(), 'secondary': r.u8()})
            settings['keyboard_bindings'] = kb
        if settings_bits & (1 << 3):
            settings['macros'] = [r.string() for _ in range(8)]
        if settings_bits & (1 << 4):
            settings['value_option'] = r.u32()
        if settings_bits & (1 << 5):
            gp_count = r.u8()
            gp = []
            for _ in range(gp_count):
                gp.append({'type': r.u8(), 'unused': r.u8(), 'primary': r.u8(), 'secondary': r.u8()})
            settings['gamepad_bindings'] = gp
        settings['age'] = r.u8()
        settings['hide_age'] = r.u8()
        info['settings'] = settings

        # Missions
        mission_count = r.u8()
        missions = []
        for _ in range(mission_count):
            m = {'id': r.u16()}
            prog_count = r.u8()
            m['progress'] = [{'id': r.u32(), 'value': r.u32()} for _ in range(prog_count)]
            missions.append(m)
        info['missions'] = missions

        # Ranch/server
        info['val6'] = r.string()
        info['ranch_address'] = r.u32()
        info['ranch_port'] = r.u16()
        info['scrambling_constant'] = r.u32()

        # Character
        char = {}
        char['char_id'] = r.u8()
        char['mouth_id'] = r.u8()
        char['face_id'] = r.u8()
        char['parts_val0'] = r.u8()
        char['voice_id'] = r.u16()
        char['head_size'] = r.u16()
        char['height'] = r.u16()
        char['thigh_volume'] = r.u16()
        char['leg_volume'] = r.u16()
        char['emblem_id'] = r.u16()
        info['character'] = char

        # Horse
        horse = {}
        horse['uid'] = r.u32()
        horse['tid'] = r.u32()
        horse['name'] = r.string()
        horse['skin_id'] = r.u8()
        horse['mane_id'] = r.u8()
        horse['tail_id'] = r.u8()
        horse['face_id'] = r.u8()
        horse['scale'] = r.u8()
        horse['leg_length'] = r.u8()
        horse['leg_volume'] = r.u8()
        horse['body_length'] = r.u8()
        horse['body_volume'] = r.u8()
        horse['agility'] = r.u32()
        horse['ambition'] = r.u32()
        horse['rush'] = r.u32()
        horse['endurance'] = r.u32()
        horse['courage'] = r.u32()
        horse['rating'] = r.u32()
        horse['class'] = r.u8()
        horse['class_progress'] = r.u8()
        horse['grade'] = r.u8()
        horse['growth_points'] = r.u16()
        cond = {}
        cond['stamina'] = r.u16()
        cond['charm'] = r.u16()
        cond['friendliness'] = r.u16()
        cond['injury_points'] = r.u16()
        cond['plenitude'] = r.u16()
        cond['body_dirty'] = r.u16()
        cond['mane_dirty'] = r.u16()
        cond['tail_dirty'] = r.u16()
        cond['attachment'] = r.u16()
        cond['boredom'] = r.u16()
        cond['body_polish'] = r.u16()
        cond['mane_polish'] = r.u16()
        cond['tail_polish'] = r.u16()
        cond['stop_amends'] = r.u16()
        horse['condition'] = cond
        horse['type'] = HORSE_TYPES.get(r.u8(), 'Unknown')
        horse['vals1_val1'] = r.u32()
        horse['date_of_birth'] = r.u32()
        horse['tendency'] = r.u8()
        horse['spirit'] = r.u8()
        horse['class_progression'] = r.u32()
        horse['vals1_val5'] = r.u32()
        horse['potential_level'] = r.u8()
        horse['potential_type'] = r.u8()
        horse['potential_value'] = r.u8()
        horse['vals1_val9'] = r.u8()
        horse['luck'] = r.u8()
        horse['injury'] = INJURIES.get(r.u8(), 'Unknown')
        horse['vals1_val12'] = r.u8()
        horse['fatigue'] = r.u16()
        horse['vals1_val14'] = r.u16()
        horse['emblem'] = r.u16()
        horse['spur_magic_count'] = r.u32()
        horse['jump_count'] = r.u32()
        horse['sliding_time'] = r.u32()
        horse['gliding_distance'] = r.u32()
        horse['val16'] = r.u32()
        horse['visual_cleanliness'] = r.u32()
        info['horse'] = horse

        # System content
        sc_count = r.u8()
        system_content = {}
        for _ in range(sc_count):
            system_content[r.u32()] = r.u32()
        info['system_content'] = system_content

        # Avatar bitfield
        info['avatar_bitfield'] = r.u32()
        info['has_played_before'] = bool(info['avatar_bitfield'] & 2)

        # Struct1
        info['struct1'] = {'val0': r.u16(), 'val1': r.u16(), 'val2': r.u16()}
        info['val10'] = r.u32()

        # Management skills
        mgmt = {'val0': r.u8(), 'progress': r.u32(), 'points': r.u16()}
        info['management_skills'] = mgmt

        # Skill ranks
        sr_count = r.u8()
        info['skill_ranks'] = [{'id': r.u8(), 'rank': r.u8()} for _ in range(sr_count)]

        # Training progression
        tp_count = r.u8()
        game_modes = {0: 'None', 1: 'Speed', 2: 'Magic'}
        clear_stages = {0: 'None', 1: 'Easy', 2: 'Normal', 3: 'Hard', 4: 'VeryHard'}
        info['training_progression'] = [
            {'map_id': r.u16(), 'game_mode': game_modes.get(r.u8(), '?'),
             'clear_stage': clear_stages.get(r.u8(), '?')}
            for _ in range(tp_count)
        ]

        # Creation date
        info['character_creation_date'] = r.u32()

        # Guild
        guild = {}
        guild['uid'] = r.u32()
        guild['val1'] = r.u8()
        guild['val2'] = r.u32()
        guild['name'] = r.string()
        guild_roles = {10: 'Owner', 100: 'Officer', 200: 'Member'}
        guild['role'] = guild_roles.get(r.u8(), 'None')
        guild['val5'] = r.u32()
        guild['val6'] = r.u8()
        info['guild'] = guild

        info['val16'] = r.u8()

        # Rent
        info['rent'] = {'mount_uid': r.u32(), 'val1': r.u32(), 'val2': r.u32()}
        info['housing_bonus'] = r.u32()
        info['val19'] = r.u32()
        info['val20'] = r.u32()

        # Pet
        pet = {'pet_id': r.u32(), 'member2': r.u32(), 'name': r.string(), 'birth_date': r.u32()}
        info['pet'] = pet

        info['bytes_parsed'] = r.pos
        info['bytes_remaining'] = r.remaining()

    except Exception as e:
        info['parse_error'] = f"{e} (at byte {r.pos})"

    return info


# ── Display ─────────────────────────────────────────────────

def display_login_ok(info: dict, server_name: str):
    def section(title):
        print(f"\n  ── {title} {'─' * (44 - len(title))}")

    section("Profile")
    print(f"    Name:          {info.get('name', '?')}")
    print(f"    UID:           {info.get('uid', '?')}")
    print(f"    Gender:        {info.get('gender', '?')}")
    print(f"    Level:         {info.get('level', '?')}")
    print(f"    Carrots:       {info.get('carrots', '?'):,}")
    print(f"    Role:          {info.get('role', '?')}")
    print(f"    Introduction:  {info.get('introduction', '') or '(none)'}")
    print(f"    Played before: {info.get('has_played_before', '?')}")

    section("Server")
    print(f"    Server:        {server_name}")
    print(f"    Notice:        {info.get('notice', '?')}")
    ranch_ip = info.get('ranch_address', 0)
    if ranch_ip:
        ip = f"{ranch_ip & 0xFF}.{(ranch_ip >> 8) & 0xFF}.{(ranch_ip >> 16) & 0xFF}.{(ranch_ip >> 24) & 0xFF}"
        print(f"    Ranch server:  {ip}:{info.get('ranch_port', '?')}")

    section("Character")
    c = info.get('character', {})
    print(f"    Char ID:       {c.get('char_id', '?')} ({'Girl' if c.get('char_id') == 20 else 'Boy' if c.get('char_id') == 10 else '?'})")
    print(f"    Voice:         {c.get('voice_id', '?')}")
    print(f"    Height:        {c.get('height', '?')}")
    print(f"    Emblem:        {c.get('emblem_id', '?')}")

    section("Horse")
    h = info.get('horse', {})
    print(f"    Name:          {h.get('name', '?')}")
    print(f"    UID:           {h.get('uid', '?')}")
    print(f"    Type:          {h.get('type', '?')}")
    print(f"    Grade:         {h.get('grade', '?')}")
    print(f"    Class:         {h.get('class', '?')}")
    print(f"    Rating:        {h.get('rating', '?')}")
    print(f"    Stats:         AGI={h.get('agility',0)} AMB={h.get('ambition',0)} "
          f"RSH={h.get('rush',0)} END={h.get('endurance',0)} CRG={h.get('courage',0)}")
    print(f"    Growth pts:    {h.get('growth_points', '?')}")
    print(f"    Injury:        {h.get('injury', 'None')}")
    print(f"    Fatigue:       {h.get('fatigue', 0)}/1500")
    print(f"    Luck:          {h.get('luck', '?')}")
    print(f"    Potential:     lvl={h.get('potential_level',0)} type={h.get('potential_type',0)} val={h.get('potential_value',0)}")

    cond = h.get('condition', {})
    if cond:
        print(f"    Stamina:       {cond.get('stamina',0)}/4000")
        print(f"    Plenitude:     {cond.get('plenitude',0)}/1200")
        print(f"    Charm:         {cond.get('charm',0)}/1000")
        print(f"    Friendliness:  {cond.get('friendliness',0)}/1000")
        print(f"    Attachment:    {cond.get('attachment',0)}")
        print(f"    Boredom:       {cond.get('boredom',0)}/21")

    print(f"    Mastery:       spur={h.get('spur_magic_count',0)} jump={h.get('jump_count',0)} "
          f"slide={h.get('sliding_time',0)} glide={h.get('gliding_distance',0)}")

    section("Equipment")
    eq = info.get('equipment', [])
    if eq:
        for item in eq:
            print(f"    Item TID={item['tid']}, UID={item['uid']}, count={item['count']}")
    else:
        print(f"    (none)")

    guild = info.get('guild', {})
    if guild.get('uid', 0):
        section("Guild")
        print(f"    Name:          {guild.get('name', '?')}")
        print(f"    Role:          {guild.get('role', '?')}")
        print(f"    UID:           {guild.get('uid', '?')}")

    pet = info.get('pet', {})
    if pet.get('pet_id', 0):
        section("Pet")
        print(f"    Name:          {pet.get('name', '?')}")
        print(f"    ID:            {pet.get('pet_id', '?')}")

    tp = info.get('training_progression', [])
    if tp:
        section("Training")
        for t in tp:
            print(f"    Map {t['map_id']:3d}: {t['game_mode']:6s} → {t['clear_stage']}")

    mgmt = info.get('management_skills', {})
    section("Management Skills")
    print(f"    Progress:      {mgmt.get('progress', 0)}/2675")
    print(f"    Points:        {mgmt.get('points', 0)}")

    sr = info.get('skill_ranks', [])
    if sr:
        print(f"    Ranks:         {', '.join(f'skill{s["id"]}=rank{s["rank"]}' for s in sr)}")

    section("Misc")
    print(f"    Settings age:  {info.get('settings', {}).get('age', '?')}")
    print(f"    Missions:      {len(info.get('missions', []))}")
    print(f"    Sys content:   {len(info.get('system_content', {}))} entries")

    if 'parse_error' in info:
        print(f"\n  ⚠ Parse stopped: {info['parse_error']}")
    else:
        print(f"\n  ✓ Fully parsed ({info.get('bytes_parsed', '?')} bytes, "
              f"{info.get('bytes_remaining', '?')} remaining)")


# ── Server Communication ───────────────────────────────────

def server_login(host: str, port: int, login_id: str, auth_key: str,
                 member_no: int = 0, timeout: float = 10.0) -> dict:
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
    except (socket.timeout, ConnectionRefusedError, OSError) as e:
        return {'status': 'offline', 'error': str(e)}

    try:
        sock.sendall(build_login_packet(login_id, auth_key, member_no))

        response = b''
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if len(response) >= 4:
                    _, resp_len = decode_message_magic(
                        struct.unpack('<I', response[:4])[0])
                    if len(response) >= resp_len:
                        break
            except socket.timeout:
                break

        if not response:
            return {'status': 'no_response'}

        cmd_id, resp_len = decode_message_magic(
            struct.unpack('<I', response[:4])[0])
        payload = response[4:resp_len]

        if cmd_id == CMD_LOGIN_OK:
            result = parse_login_ok(payload)
            result['status'] = 'ok'
            return result
        elif cmd_id == CMD_LOGIN_CANCEL:
            reason = payload[0] if payload else -1
            return {'status': 'rejected', 'reason_code': reason,
                    'reason': LOGIN_CANCEL_REASONS.get(reason, f'Unknown({reason})')}
        else:
            return {'status': 'unexpected', 'cmd_id': cmd_id}

    except Exception as e:
        return {'status': 'error', 'error': str(e)}
    finally:
        sock.close()


# ── Auth Flows ──────────────────────────────────────────────

def auth_soa() -> tuple[str, str, int] | None:
    """Story of Alicia auth: Discord OAuth → token."""
    print(f"\n  1. Open in browser:")
    print(f"     {SOA_OAUTH_URL}\n")
    print(f"  2. Authorize with Discord, then copy the redirect URL.")
    print(f"     (looks like: https://storyofalicia.com/launcher/handoff/?token=...)\n")

    url = input("  Paste URL (or Enter to cancel): ").strip()
    if not url:
        return None

    params = parse_qs(urlparse(url).query)
    token = params.get('token', [None])[0]
    user_id = params.get('user', [None])[0]
    username = params.get('username', [None])[0]

    if not token or not user_id:
        print("  Could not parse token/user from URL.")
        return None

    print(f"\n  Player:     {username}")
    print(f"  Discord ID: {user_id}")
    print(f"  Token:      {token[:24]}...")

    return user_id, token, 0

# ── Self Tests ──────────────────────────────────────────────

def run_tests() -> bool:
    print("── Protocol Tests ─────────────────────────────────\n")
    ok = True

    cases = [(0x7, 4), (0x7, 101), (0x12, 4), (0x8, 1134), (0x9, 5)]
    for cmd, ln in cases:
        enc = encode_message_magic(cmd, ln)
        d_cmd, d_ln = decode_message_magic(enc)
        passed = d_cmd == cmd and d_ln == ln
        ok &= passed
        print(f"  {'✓' if passed else '✗'} magic roundtrip cmd=0x{cmd:X} len={ln}")

    c = roll_code(0)
    passed = c == XOR_CONTROL
    ok &= passed
    print(f"  {'✓' if passed else '✗'} first roll = 0x{c:08X}")

    key = u32_bytes(XOR_CONTROL)
    orig = b"Hello, Alicia!"
    passed = xor_bytes(key, xor_bytes(key, orig)) == orig
    ok &= passed
    print(f"  {'✓' if passed else '✗'} XOR roundtrip")

    print(f"\n  {'All tests passed.' if ok else 'SOME TESTS FAILED.'}\n")
    return ok


# ── Main ────────────────────────────────────────────────────

def main():
    skip_tests = "--skip-tests" in sys.argv

    print()
    print("  ╔═════════════════════════════════════════════════╗")
    print("  ║         Story Of Alicia - Protocol Client       ║")
    print("  ╚═════════════════════════════════════════════════╝")
    print()

    if not skip_tests:
        run_tests()

    server = SERVERS['soa']
    creds = auth_soa()

    if not creds:
        print("  Cancelled.\n")
        return

    login_id, auth_key, member_no = creds

    print(f"\n── Connecting to {server['name']} ({server['host']}:{server['port']}) ──")

    result = server_login(server['host'], server['port'], login_id, auth_key, member_no)

    if result['status'] == 'ok':
        print(f"\n  ✓ LOGIN SUCCESSFUL")
        display_login_ok(result, server['name'])
    elif result['status'] == 'rejected':
        print(f"\n  ✗ Login rejected: {result['reason']}")
    elif result['status'] == 'offline':
        print(f"\n  ✗ Server offline: {result.get('error', '?')}")
    else:
        print(f"\n  ✗ {result['status']}: {result}")

    print()


if __name__ == "__main__":
    main()