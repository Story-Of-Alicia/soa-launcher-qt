#!/usr/bin/env python3
# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

# Story of Alicia protocol client
# Logs into the game server like the game does and prints everything it gets back
# about your account, character and horse. Im using this to figure out the data
# for the player menu in the launcher.
#
# The protocol is from alicia-server: https://github.com/Story-Of-Alicia/alicia-server
#
# How to use it:
#   python3 alicia_client.py [--skip-tests] [--json FILE] [--server-repo PATH]
#
#   --skip-tests        dont run the self tests first
#   --json FILE         saves everything it read to a json file too
#   --server-repo PATH  path to your alicia-server folder, so items get names

import json
import os
import re
import struct
import socket
import sys
from datetime import datetime, timezone
from urllib.parse import urlparse, parse_qs

# Protocol constants

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
# The server only saves the points above 20, the game adds 20 to every stat
# when it shows them. Checked it against the Horse Profile in game.
HORSE_STAT_BASE = 20

# Max values from alicia-server (include/server/system/HorseSystem.hpp)
MAX_STAMINA = 4000
MAX_FATIGUE = 1500
MAX_PLENITUDE = 1000
MAX_CHARM = 1000
MAX_FRIENDLINESS = 1000
MAX_ATTACHMENT = 1000
MAX_BOREDOM = 25

# Exp needed per class up, from alicia-server
# resources/config/game/horses/progression.yaml (levelUpPoints)
# class 1 to 10
CLASS_EXP_BASE = 6650
# class 11 to 20
CLASS_EXP_10 = 13250
# class 21 to 30
CLASS_EXP_20 = 16550
MAX_CLASS = 30

INJURIES = {
    0: "None", 17: "Minor Muscle Strain", 18: "Severe Muscle Strain",
    33: "Minor Wounds", 34: "Severe Wounds",
    65: "Minor Fracture", 66: "Severe Fracture",
}

# Everything under here is from alicia-server too, I wrote down the files so
# its easy to check again if the server changes something.

# Character levels, resources/config/game/character.yaml (levelInfo)
# Index 0 is level 1. These are total exp, same as levelProgress from the server.
CHARACTER_LEVEL_EXP = [
    0, 2000, 4200, 6600, 9200, 12000, 14800, 17600, 20600, 24600,
    29000, 34400, 39400, 44800, 50800, 58000, 67600, 76000, 84800, 94800,
    106000, 118800, 131200, 145000, 160200, 176800, 196300, 214100, 233300, 254500,
    277100, 302300, 326700, 353100, 382100, 413100, 448100, 481500, 517900, 557300,
    599100, 645000, 689200, 736600, 787200, 841600, 901000, 957800, 1018200, 1082200,
    1150400, 1223700, 1294900, 1369900, 1450900, 1529500, 1612300, 1699500, 1791100, 1888300,
    1982900, 2082300, 2186100, 2294300, 2409500, 2521300, 2638900, 2761300, 2888500, 3022400,
    3153800, 3290400, 3432400, 3579200, 3734000, 3885000, 4042200, 4205200, 4373400, 4548900,
    4721900, 4901100, 5086100, 5276900, 5475600, 5671200, 5874000, 6083000, 6298200, 6519500,
    6740100, 6967900, 7201900, 7442700, 7689400, 7935600, 8189000, 8449200, 8715600, 8988800,
]
MAX_CHARACTER_LEVEL = len(CHARACTER_LEVEL_EXP)

# Potential names, resources/config/game/horses/potential.yaml (theres no 12)
POTENTIALS = {
    0: "None", 1: "Natural Talent", 2: "Speed Addict", 3: "Lucky Charm", 4: "Iron Heart",
    5: "One Plus One", 6: "Superior Reward", 7: "Perfectionist", 8: "Sniper",
    9: "Iron Wall", 10: "Opportunity", 11: "Fearless", 13: "Superior Start",
    14: "Two Hearts", 15: "Bulldozer",
}

# System content names, resources/config/server/system_content.yaml
SYSTEM_CONTENT = {
    0: "Speed Solo", 1: "Speed Team", 2: "Magic Solo", 3: "Magic Team",
    4: "Entering the store", 5: "Shop purchases", 6: "Team Battle 1 vs 1",
    7: "Stop service: Maintenance", 8: "[Packet] Server 8T(7.5/s)",
    9: "[Packet] Server 15T(4/s)", 10: "[Packet] P2P 6T (10/s)", 11: "[Packet] P2P 8T(7.5/s)",
    12: "[Packet] Pasture 15T(4/s)", 13: "Display AI player checkbox in the waiting room",
    14: "Festival activation", 15: "Channel window game mode", 16: "Allow singleplayer",
    17: "Game countdown [ms]", 18: "Exp&Carrot Boost [%]", 19: "Always a new record for ghost",
    20: "Mating", 21: "Training", 22: "Singleplayer", 23: "Ranch management",
    24: "Starting injury level", 25: "Is TCP relay working?", 26: "How many times to send via TCP?",
    27: "Block detection time (s) before TCP Relay starts", 28: "Speed Team entry level",
    29: "Magic Team entry level", 30: "Whether 2fa works", 31: "2fa mating", 32: "2fa - send gift",
    33: "2fa return to nature", 34: "Real-time server statistics", 35: "Adjust horse size",
    36: "PC room premium horse rental", 37: "Experience/carrots boost",
    38: "PC room premium regen cycle [min]",
    39: "Entry validity period upon invitation/summon (def:180 seconds)", 40: "Stamp Event",
    41: "Pet system", 42: "Request League Ranking (client only)", 43: "Use cash recharge ?",
    44: "Display webpage when exiting ?", 45: "Monday league tally", 46: "Tuesday league tally",
    47: "Wednesday league tally", 48: "Thursday league tally", 49: "Friday league tally",
    50: "Saturday league tally", 51: "Sunday league tally", 52: "Guild Match start time",
    53: "Guild Match duration", 54: "Guilds", 55: "Match interval (U:seconds, Def:30)",
    56: "Field Item Acquisiton event", 57: "Ranch Shepherd Carrot Payment",
    58: "Minimum people required for guild match", 59: "Weekly reset day",
    60: "Weekly reset hour(0~23)", 61: "Min level required to acquire eggs (during map generation)",
    62: "Shutdown system", 63: "Default visit ranch UID (for testing)",
    64: "Egg hatching/boosting time ratio (10000%)", 65: "Ranch Weather Features",
    66: "In-Game Weather", 67: "Force random weather during match", 68: "PC specification log",
}

# CommonStructureDefinitions.hpp
GAME_MODES = {0: "None", 1: "Speed", 2: "Magic", 4: "Unk4", 6: "Mission"}
CLEAR_STAGES = {0: "None", 1: "Easy", 2: "Normal", 3: "Hard", 4: "VeryHard"}
GUILD_ROLES = {10: "Owner", 100: "Officer", 200: "Member"}
CHARACTER_MODELS = {10: "Boy", 20: "Girl"}

# Settings::Type, which bit in the settings bitset is what
SETTINGS_KEYBOARD = 0
SETTINGS_MACROS = 3
SETTINGS_VALUE = 4
SETTINGS_GAMEPAD = 5

# LobbyCommandLoginOK::AvatarBitset, the server just sends 0 or 2
AVATAR_NEW_PLAYER = 0
AVATAR_INTRO_COMPLETED = 2

# Horse::VisualCleanlinessBitset
VISUAL_CLEANLINESS = {
    0: "BodySlightlyDirty", 1: "BodyVeryDirty",
    2: "BodyLightSparkles", 3: "BodyMediumSparkles", 4: "BodyHeavySparkles",
    10: "ManeSlightlyDirty", 11: "ManeVeryDirty",
    12: "ManeLightSparkles", 13: "ManeMediumSparkles", 14: "ManeHeavySparkles",
    20: "TailSlightlyDirty", 21: "TailVeryDirty",
    22: "TailLightSparkles", 23: "TailMediumSparkles", 24: "TailHeavySparkles",
}

# Thresholds for hungry, dirty and so on. From the MountCondition
# comments and ProtocolHelper.cpp in alicia-server.
PLENITUDE_SLIGHTLY_FULL = 710
DIRTY_SLIGHTLY = 600
DIRTY_VERY = 1000
POLISH_SPARKLES = 100
# unlocks above this, server says it might be random
ATTACHMENT_FISHING_ROD = 111
# same as above
ATTACHMENT_BOW = 501

# Max management skills progress, from a comment in LobbyMessageDefinitions.hpp
# (CareSkillLevel table)
MAX_MANAGEMENT_PROGRESS = 2675

# You get the ranch bonus every 20th race (RanchManagement::totalRaces)
RANCH_BONUS_EVERY = 20

# The server always sends this as the horses val16, unknown what it is
HORSE_VAL16_CONSTANT = 0xB8A167E4

# Protocol core

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


# Stream reader

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


# Game formulas

def cumulative_class_exp(horse_class: int) -> int:
    # Total class exp you need to reach a class, works the same as
    # HorseRegistry::GetCumulativeClassExp in alicia-server
    if horse_class <= 1:
        return 0
    steps = horse_class - 1
    total = min(steps, 10) * CLASS_EXP_BASE
    if steps > 10:
        total += min(steps - 10, 10) * CLASS_EXP_10
    if steps > 20:
        total += (steps - 20) * CLASS_EXP_20
    return total


def class_progress_percent(horse_class: int, class_exp: int) -> float:
    # How close the horse is to the next class, like the green bar in game
    if horse_class >= MAX_CLASS:
        return 100.0
    start = cumulative_class_exp(horse_class)
    end = cumulative_class_exp(horse_class + 1)
    return max(0.0, min(100.0, (class_exp - start) * 100.0 / (end - start)))


def ipv4_from_u32(value: int) -> str:
    # The ip bytes come in reversed compared to how we read the u32,
    # so pack it big endian to get it in the right order
    return socket.inet_ntoa(struct.pack('>I', value & 0xFFFFFFFF))


def online_players_from_notice(notice: str) -> int | None:
    # The notice looks like "Players online: 228"
    if ':' not in notice:
        return None
    try:
        return int(notice.rsplit(':', 1)[1].strip())
    except ValueError:
        return None


def character_level_percent(level: int, experience: int) -> float:
    # How close you are to the next level (CharacterRegistry in alicia-server)
    if level >= MAX_CHARACTER_LEVEL:
        return 100.0
    if level < 1:
        return 0.0
    start = CHARACTER_LEVEL_EXP[level - 1]
    end = CHARACTER_LEVEL_EXP[level]
    return max(0.0, min(100.0, (experience - start) * 100.0 / (end - start)))


def filetime_to_text(low: int, high: int) -> str:
    # Windows FILETIME (100ns steps since 1601) to a utc date
    value = (high << 32) | low
    if value == 0:
        return "(none)"
    seconds = value / 10_000_000 - 11_644_473_600
    return datetime.fromtimestamp(seconds, timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')


def alicia_time(value: int) -> dict | None:
    # The game packs dates into a u32 (util::DateTimeToAliciaTime)
    # bits: year 0-11, month 12-15, day 16-20, hour 21-25, minute 26-31
    if value == 0:
        return None
    return {
        'year': value & 0xFFF,
        'month': (value >> 12) & 0xF,
        'day': (value >> 16) & 0x1F,
        'hour': (value >> 21) & 0x1F,
        'minute': (value >> 26) & 0x3F,
    }


def alicia_time_text(value: int) -> str:
    t = alicia_time(value)
    if t is None:
        return "(none)"
    return f"{t['year']:04d}-{t['month']:02d}-{t['day']:02d} {t['hour']:02d}:{t['minute']:02d}"


def bit_names(value: int, names: dict) -> list[str]:
    return [name for bit, name in names.items() if value & (1 << bit)]


def plenitude_state(value: int) -> str:
    if value >= MAX_PLENITUDE:
        return "Full"
    if value >= PLENITUDE_SLIGHTLY_FULL:
        return "Slightly full"
    return "Hungry"


def dirtiness_state(value: int) -> str:
    if value >= DIRTY_VERY:
        return "Very dirty"
    if value >= DIRTY_SLIGHTLY:
        return "Slightly dirty"
    return "Clean"


def boredom_state(value: int) -> str:
    # Higher is better here, 21+ means the horse wants to play
    if value >= 21:
        return "Wants to play"
    if value >= 11:
        return "Wants to play a little"
    if value >= 1:
        return "A little bored"
    return "Bored"


def attachment_unlocks(value: int) -> list[str]:
    unlocks = []
    if value > ATTACHMENT_FISHING_ROD:
        unlocks.append("Fishing rod")
    if value > ATTACHMENT_BOW:
        unlocks.append("Bow")
    return unlocks


def load_item_names(server_repo: str) -> dict[int, str]:
    # Gets item names from your alicia-server folder (resources/config/game/items/items.yaml)
    # Optional, only used to show item names next to the tid
    path = os.path.join(server_repo, 'resources', 'config', 'game', 'items', 'items.yaml')
    try:
        with open(path, encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return {}
    names = {}
    for tid, name in re.findall(r"^- tid: (\d+)\n(?:  .*\n)*?  name: '((?:[^']|'')*)'", text, re.M):
        names[int(tid)] = name.replace("''", "'")
    return names


# Packet building

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


# LoginOK parser

def read_item(r: StreamReader) -> dict:
    item = {'uid': r.u32(), 'tid': r.u32(), 'expires': r.u32(), 'count': r.u32()}
    # expiresAt is a packed date (when you got it + how long it lasts)
    item['expires_date'] = alicia_time(item['expires'])
    return item


def read_bindings(r: StreamReader) -> list[dict]:
    count = r.u8()
    return [{'type': r.u8(), 'unused': r.u8(), 'primary': r.u8(), 'secondary': r.u8()}
            for _ in range(count)]


def parse_login_ok(data: bytes) -> dict:
    # LobbyCommandLoginOK, read in the same order LobbyMessageDefinitions.cpp
    # writes it. Names in the comments are what the server calls them.
    r = StreamReader(data)
    info = {}

    try:
        # lobbyTime: the servers time right now as a Windows FILETIME
        info['lobby_time_low'] = r.u32()
        info['lobby_time_high'] = r.u32()
        info['lobby_time'] = filetime_to_text(info['lobby_time_low'], info['lobby_time_high'])
        # member0: unknown, its commented out in the server (0xCA794) so its 0
        info['member0'] = r.u32()

        # Profile
        info['uid'] = r.u32()
        # max 16 long
        info['name'] = r.string()
        # max 255 long, looks like "Players online: N"
        info['notice'] = r.string()
        info['online_players'] = online_players_from_notice(info['notice'])
        info['gender_id'] = r.u8()
        info['gender'] = GENDERS.get(info['gender_id'], 'Unknown')
        # max 255 long
        info['introduction'] = r.string()

        # equipmentItems (max 16) and expiredItems (max 250)
        info['equipment'] = [read_item(r) for _ in range(r.u8())]
        info['expired_items'] = [read_item(r) for _ in range(r.u8())]

        # Level and carrots
        info['level'] = r.u16()
        info['carrots'] = r.i32()
        # levelProgress: total character exp, its 0 in the prologue
        info['level_progress'] = r.u32()
        info['level_percent'] = character_level_percent(info['level'], info['level_progress'])
        info['role_id'] = r.u32()
        info['role'] = ROLES.get(info['role_id'], 'Unknown')
        # val3: unknown, the server sends 0
        info['val3'] = r.u8()

        # Settings, only the parts in typeBitset get sent
        settings_bits = r.u32()
        settings = {'type_bits': settings_bits}
        if settings_bits & (1 << SETTINGS_KEYBOARD):
            settings['keyboard_bindings'] = read_bindings(r)
        if settings_bits & (1 << SETTINGS_MACROS):
            settings['macros'] = [r.string() for _ in range(8)]
        if settings_bits & (1 << SETTINGS_VALUE):
            # valueOption: gets sent every time you close the settings window
            settings['value_option'] = r.u32()
        if settings_bits & (1 << SETTINGS_GAMEPAD):
            settings['gamepad_bindings'] = read_bindings(r)
        settings['age'] = r.u8()
        settings['hide_age'] = r.u8()
        info['settings'] = settings

        # Missions (max 17)
        missions = []
        for _ in range(r.u8()):
            m = {'id': r.u16()}
            m['progress'] = [{'id': r.u32(), 'value': r.u32()} for _ in range(r.u8())]
            missions.append(m)
        info['missions'] = missions

        # val6: unknown string (max 256), the server sends it empty
        info['val6'] = r.string()

        # Ranch server
        info['ranch_address'] = r.u32()
        info['ranch_ip'] = ipv4_from_u32(info['ranch_address'])
        info['ranch_port'] = r.u16()
        # scramblingConstant: the server sends 0
        info['scrambling_constant'] = r.u32()

        # Character::Parts (from the _ClientCharDefaultPartInfo table in the game)
        char = {}
        # 10 boy, 20 girl
        char['char_id'] = r.u8()
        char['model'] = CHARACTER_MODELS.get(char['char_id'], 'Unknown')
        # mouthSerialId, FaceId in the client
        char['mouth_id'] = r.u8()
        # faceSerialId, EyeId in the client
        char['face_id'] = r.u8()
        # unknown
        char['parts_val0'] = r.u8()
        # Character::Appearance
        char['voice_id'] = r.u16()
        # FigFace in the game
        char['head_size'] = r.u16()
        # FigTall in the game
        char['height'] = r.u16()
        # FigVolume in the game
        char['thigh_volume'] = r.u16()
        # FigShape in the game
        char['leg_volume'] = r.u16()
        char['emblem_id'] = r.u16()
        info['character'] = char

        # Horse
        horse = {}
        horse['uid'] = r.u32()
        horse['tid'] = r.u32()
        horse['name'] = r.string()
        # Horse::Parts
        horse['skin_id'] = r.u8()
        horse['mane_id'] = r.u8()
        horse['tail_id'] = r.u8()
        horse['face_id'] = r.u8()
        # Horse::Appearance (figure)
        horse['scale'] = r.u8()
        horse['leg_length'] = r.u8()
        horse['leg_volume'] = r.u8()
        horse['body_length'] = r.u8()
        horse['body_volume'] = r.u8()
        # Horse::Stats, the server sends agility, ambition, rush, endurance, courage.
        # In game they are Agility, Spirit, Speed, Strength and Control, +20 each.
        horse['agility'] = r.u32() + HORSE_STAT_BASE
        # ambition in the server
        horse['spirit'] = r.u32() + HORSE_STAT_BASE
        # rush in the server
        horse['speed'] = r.u32() + HORSE_STAT_BASE
        # endurance in the server
        horse['strength'] = r.u32() + HORSE_STAT_BASE
        # courage in the server
        horse['control'] = r.u32() + HORSE_STAT_BASE
        horse['rating'] = r.u32()
        # clazz in the server
        horse['class'] = r.u8()
        # always 1, this is NOT class progress
        horse['val0'] = r.u8()
        horse['grade'] = r.u8()
        horse['growth_points'] = r.u16()

        # Horse::MountCondition
        cond = {}
        # max 4000
        cond['stamina'] = r.u16()
        # charmPoint in the server, max 1000
        cond['charm'] = r.u16()
        # friendlyPoint in the server, max 1000
        cond['friendliness'] = r.u16()
        # injuryPoint in the server
        cond['injury_points'] = r.u16()
        # max 1000
        cond['plenitude'] = r.u16()
        # bodyDirtiness in the server, max 1000
        cond['body_dirty'] = r.u16()
        # maneDirtiness in the server, the game calls it ManeTwisted
        cond['mane_dirty'] = r.u16()
        # tailDirtiness in the server, the game calls it TailTwisted
        cond['tail_dirty'] = r.u16()
        # also called trust
        cond['attachment'] = r.u16()
        # max 25, higher is better here
        cond['boredom'] = r.u16()
        # 100 or more gives sparkles
        cond['body_polish'] = r.u16()
        cond['mane_polish'] = r.u16()
        cond['tail_polish'] = r.u16()
        # stopAmendsPoint, unknown
        cond['stop_amends'] = r.u16()
        cond['plenitude_state'] = plenitude_state(cond['plenitude'])
        cond['body_state'] = dirtiness_state(cond['body_dirty'])
        cond['mane_state'] = dirtiness_state(cond['mane_dirty'])
        cond['tail_state'] = dirtiness_state(cond['tail_dirty'])
        cond['boredom_state'] = boredom_state(cond['boredom'])
        cond['attachment_unlocks'] = attachment_unlocks(cond['attachment'])
        horse['condition'] = cond

        # Horse::vals1
        horse['type_id'] = r.u8()
        horse['type'] = HORSE_TYPES.get(horse['type_id'], 'Unknown')
        # unknown, the server sends 0
        horse['vals1_val1'] = r.u32()
        # packed date
        horse['date_of_birth'] = r.u32()
        horse['date_of_birth_date'] = alicia_time(horse['date_of_birth'])
        # 1-6, from the client's Tendency table
        horse['tendency'] = r.u8()
        # called spirit in the server but its NOT the Spirit stat, unknown
        horse['vals1_spirit'] = r.u8()
        # classProgression, all the class exp the horse ever got
        horse['class_exp'] = r.u32()
        horse['class_percent'] = class_progress_percent(horse['class'], horse['class_exp'])
        # unknown, the server sends 0
        horse['vals1_val5'] = r.u32()
        horse['potential_level'] = r.u8()
        horse['potential_type'] = r.u8()
        horse['potential_name'] = POTENTIALS.get(horse['potential_type'], 'Unknown')
        horse['potential_value'] = r.u8()
        # unknown, the server sends 0
        horse['vals1_val9'] = r.u8()
        # luckState, the server has no names for these
        horse['luck'] = r.u8()
        # the server always sends 0 (None) for now
        horse['injury_id'] = r.u8()
        horse['injury'] = INJURIES.get(horse['injury_id'], 'Unknown')
        # unknown, the server sends 0
        horse['vals1_val12'] = r.u8()
        # max 1500
        horse['fatigue'] = r.u16()
        # unknown, the server sends 0
        horse['vals1_val14'] = r.u16()
        # emblemUid in the server
        horse['emblem'] = r.u16()

        # Horse::Mastery
        horse['spur_magic_count'] = r.u32()
        horse['jump_count'] = r.u32()
        horse['sliding_time'] = r.u32()
        # maybe x10? the server isnt sure either
        horse['gliding_distance'] = r.u32()

        # unknown, always 0xB8A167E4
        horse['val16'] = r.u32()
        horse['visual_cleanliness'] = r.u32()
        horse['visual_cleanliness_flags'] = bit_names(horse['visual_cleanliness'], VISUAL_CLEANLINESS)
        info['horse'] = horse

        # systemContent: key -> value, the names are in SYSTEM_CONTENT
        system_content = {}
        for _ in range(r.u8()):
            key = r.u32()
            system_content[key] = r.u32()
        info['system_content'] = system_content

        # bitfield (AvatarBitset): 0 is a new player, 2 means the intro is done
        info['avatar_bitfield'] = r.u32()
        info['has_played_before'] = bool(info['avatar_bitfield'] & AVATAR_INTRO_COMPLETED)

        # val9 (Struct1): unknown, the server sends zeros
        info['struct1'] = {'val0': r.u16(), 'val1': r.u16(), 'val2': r.u16()}
        # val10: unknown, the server sends 0
        info['val10'] = r.u32()

        # managementSkills
        info['management_skills'] = {
            # unknown
            'val0': r.u8(),
            # from the CareSkillLevel table, max 2675
            'progress': r.u32(),
            'points': r.u16(),
        }

        # skillRanks (the ids are from the CareSkillInfo table in the game)
        info['skill_ranks'] = [{'id': r.u8(), 'rank': r.u8()} for _ in range(r.u8())]

        # trainingProgression
        training = []
        for _ in range(r.u8()):
            # mapBlockId in the server
            map_id = r.u16()
            mode = r.u8()
            stage = r.u8()
            training.append({'map_id': map_id,
                             'game_mode_id': mode, 'game_mode': GAME_MODES.get(mode, '?'),
                             'clear_stage_id': stage, 'clear_stage': CLEAR_STAGES.get(stage, '?')})
        info['training_progression'] = training

        # characterCreationDate: packed date, the server doesnt save it yet so its 0
        info['character_creation_date'] = r.u32()
        info['character_creation'] = alicia_time(info['character_creation_date'])

        # Guild
        guild = {}
        guild['uid'] = r.u32()
        # unknown (commented as 1 in the server)
        guild['val1'] = r.u8()
        # maybe the emblem uid, the server isnt sure
        guild['val2'] = r.u32()
        guild['name'] = r.string()
        # guildRole in the server
        guild['role_id'] = r.u8()
        guild['role'] = GUILD_ROLES.get(guild['role_id'], 'None')
        # unknown
        guild['val5'] = r.u32()
        # the game might ignore this, the server isnt sure
        guild['val6'] = r.u8()
        info['guild'] = guild

        # val16: unknown (commented as 4 in the server)
        info['val16'] = r.u8()

        # val17 (Rent): "something with rental horse", the server sends zeros
        info['rent'] = {
            'mount_uid': r.u32(),
            # commented as tid 0x12 in the server
            'val1': r.u32(),
            # commented as 0x16e67e4 in the server
            'val2': r.u32(),
        }

        # ranchBonusRaceCount: all the races you ever finished, you get the
        # ranch bonus every 20th race
        info['ranch_bonus_race_count'] = r.u32()
        info['races_until_ranch_bonus'] = (
                RANCH_BONUS_EVERY - info['ranch_bonus_race_count'] % RANCH_BONUS_EVERY)
        # unknown (commented as 0x38d)
        info['val19'] = r.u32()
        # unknown (commented as 0x1c7)
        info['val20'] = r.u32()

        # Pet
        pet = {
            'pet_id': r.u32(),
            # unused, the server sends 0
            'member2': r.u32(),
            'name': r.string(),
            # packed date
            'birth_date': r.u32(),
        }
        pet['birth'] = alicia_time(pet['birth_date']) if pet['pet_id'] else None
        info['pet'] = pet

        info['bytes_parsed'] = r.pos
        info['bytes_remaining'] = r.remaining()

    except Exception as e:
        info['parse_error'] = f"{e} (at byte {r.pos})"

    return info


# Display

def display_login_ok(info: dict, server_name: str, item_names: dict[int, str] | None = None):
    item_names = item_names or {}

    def section(title):
        print(f"\n  ── {title} {'─' * (44 - len(title))}")

    def item_text(item):
        name = item_names.get(item['tid'])
        label = f"{name} (TID {item['tid']})" if name else f"TID {item['tid']}"
        return (f"{label}, UID {item['uid']}, count {item['count']}, "
                f"expires {alicia_time_text(item['expires'])}")

    section("Profile")
    level = info.get('level', 0)
    print(f"    Name:          {info.get('name', '?')}")
    print(f"    UID:           {info.get('uid', '?')}")
    print(f"    Gender:        {info.get('gender', '?')}")
    print(f"    Level:         {level} ({info.get('level_percent', 0):.2f}%, "
          f"{info.get('level_progress', 0):,} exp)")
    print(f"    Carrots:       {info.get('carrots', 0):,}")
    print(f"    Role:          {info.get('role', '?')}")
    print(f"    Introduction:  {info.get('introduction', '') or '(none)'}")
    print(f"    Played before: {info.get('has_played_before', '?')}")
    print(f"    Created:       {alicia_time_text(info.get('character_creation_date', 0))}")
    print(f"    Ranch bonus:   {info.get('ranch_bonus_race_count', 0):,} races, "
          f"{info.get('races_until_ranch_bonus', '?')} until the next bonus")

    section("Server")
    print(f"    Server:        {server_name}")
    print(f"    Notice:        {info.get('notice', '') or '(none)'}")
    if info.get('online_players') is not None:
        print(f"    Online:        {info['online_players']:,} players")
    print(f"    Server time:   {info.get('lobby_time', '?')}")
    if info.get('ranch_address', 0):
        print(f"    Ranch server:  {info.get('ranch_ip', '?')}:{info.get('ranch_port', '?')}")

    section("Character")
    c = info.get('character', {})
    print(f"    Model:         {c.get('char_id', '?')} ({c.get('model', '?')})")
    print(f"    Face/Eyes:     {c.get('mouth_id', '?')} / {c.get('face_id', '?')}")
    print(f"    Voice:         {c.get('voice_id', '?')}")
    print(f"    Figure:        head {c.get('head_size', '?')}, height {c.get('height', '?')}, "
          f"thighs {c.get('thigh_volume', '?')}, legs {c.get('leg_volume', '?')}")
    print(f"    Emblem:        {c.get('emblem_id', '?')}")

    section("Horse")
    h = info.get('horse', {})
    print(f"    Name:          {h.get('name', '?')}")
    print(f"    UID / TID:     {h.get('uid', '?')} / {h.get('tid', '?')}")
    print(f"    Type:          {h.get('type', '?')}")
    print(f"    Born:          {alicia_time_text(h.get('date_of_birth', 0))}")
    print(f"    Grade:         {h.get('grade', '?')}")
    print(f"    Class:         {h.get('class', 0)} ({h.get('class_percent', 0):.2f}%, "
          f"{h.get('class_exp', 0):,} exp)")
    print(f"    Rating:        {h.get('rating', '?')}")
    stats = [('Agility', 'agility'), ('Control', 'control'), ('Speed', 'speed'),
             ('Strength', 'strength'), ('Spirit', 'spirit')]
    print(f"    Total stats:   {sum(h.get(key, 0) for _, key in stats)}")
    for label, key in stats:
        print(f"      {label + ':':<12} {h.get(key, 0)}")
    print(f"    Growth pts:    {h.get('growth_points', '?')}")
    print(f"    Tendency:      {h.get('tendency', '?')}")
    print(f"    Luck:          {h.get('luck', '?')}")
    print(f"    Injury:        {h.get('injury', 'None')}")
    print(f"    Fatigue:       {h.get('fatigue', 0)}/{MAX_FATIGUE}")
    if h.get('potential_level', 0) > 0:
        print(f"    Potential:     {h.get('potential_name', '?')}, level {h['potential_level']}, "
              f"{h.get('potential_value', 0)}/100")
    else:
        print(f"    Potential:     none")
    print(f"    Emblem:        {h.get('emblem', '?')}")
    print(f"    Looks:         skin {h.get('skin_id', '?')}, mane {h.get('mane_id', '?')}, "
          f"tail {h.get('tail_id', '?')}, face {h.get('face_id', '?')}")
    print(f"    Figure:        scale {h.get('scale', '?')}, legs {h.get('leg_length', '?')}/"
          f"{h.get('leg_volume', '?')}, body {h.get('body_length', '?')}/{h.get('body_volume', '?')}")

    cond = h.get('condition', {})
    if cond:
        print(f"    Stamina:       {cond['stamina']}/{MAX_STAMINA}")
        print(f"    Plenitude:     {cond['plenitude']}/{MAX_PLENITUDE} ({cond['plenitude_state']})")
        print(f"    Charm:         {cond['charm']}/{MAX_CHARM}")
        print(f"    Friendliness:  {cond['friendliness']}/{MAX_FRIENDLINESS}")
        unlocks = ', '.join(cond['attachment_unlocks']) or 'nothing yet'
        print(f"    Attachment:    {cond['attachment']}/{MAX_ATTACHMENT} (unlocked: {unlocks})")
        print(f"    Boredom:       {cond['boredom']}/{MAX_BOREDOM} ({cond['boredom_state']})")
        print(f"    Body:          {cond['body_dirty']} dirt, {cond['body_polish']} polish ({cond['body_state']})")
        print(f"    Mane:          {cond['mane_dirty']} dirt, {cond['mane_polish']} polish ({cond['mane_state']})")
        print(f"    Tail:          {cond['tail_dirty']} dirt, {cond['tail_polish']} polish ({cond['tail_state']})")
        print(f"    Injury points: {cond['injury_points']}")
    flags = ', '.join(h.get('visual_cleanliness_flags', [])) or 'clean, no sparkles'
    print(f"    Looks clean:   {flags}")

    print(f"    Mastery:       spur={h.get('spur_magic_count', 0)} jump={h.get('jump_count', 0)} "
          f"slide={h.get('sliding_time', 0)} glide={h.get('gliding_distance', 0)}")

    section("Equipment")
    eq = info.get('equipment', [])
    for item in eq:
        print(f"    {item_text(item)}")
    if not eq:
        print(f"    (none)")
    expired = info.get('expired_items', [])
    if expired:
        print(f"    Expired:")
        for item in expired:
            print(f"      {item_text(item)}")

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
        print(f"    Born:          {alicia_time_text(pet.get('birth_date', 0))}")

    tp = info.get('training_progression', [])
    if tp:
        section("Training")
        for t in tp:
            print(f"    Map {t['map_id']:3d}: {t['game_mode']:7s} → {t['clear_stage']}")

    mgmt = info.get('management_skills', {})
    section("Management Skills")
    print(f"    Progress:      {mgmt.get('progress', 0)}/{MAX_MANAGEMENT_PROGRESS}")
    print(f"    Points:        {mgmt.get('points', 0)}")
    sr = info.get('skill_ranks', [])
    if sr:
        print(f"    Ranks:         {', '.join(f'skill {s["id"]} = rank {s["rank"]}' for s in sr)}")

    section("Settings")
    s = info.get('settings', {})
    print(f"    Age:           {s.get('age', '?')} (hidden: {bool(s.get('hide_age', 0))})")
    print(f"    Keybinds:      {len(s.get('keyboard_bindings', []))} keyboard, "
          f"{len(s.get('gamepad_bindings', []))} gamepad")
    if 'value_option' in s:
        print(f"    Value option:  {s['value_option']}")
    macros = [m for m in s.get('macros', []) if m]
    if macros:
        print(f"    Macros:        {' | '.join(macros)}")

    missions = info.get('missions', [])
    if missions:
        section("Missions")
        for m in missions:
            progress = ', '.join(f"{p['id']}={p['value']}" for p in m['progress']) or 'no progress'
            print(f"    Mission {m['id']}: {progress}")

    sc = info.get('system_content', {})
    if sc:
        section("System Content")
        for key in sorted(sc):
            print(f"    {key:3d} {SYSTEM_CONTENT.get(key, '?')}: {sc[key]}")

    # Unknown values, printed so its easy to see if one changes
    section("Unknown Values")
    rent = info.get('rent', {})
    struct1 = info.get('struct1', {})
    unknown = [
        ('member0', info.get('member0')),
        ('val3', info.get('val3')),
        ('val6', repr(info.get('val6', ''))),
        ('scrambling_constant', info.get('scrambling_constant')),
        ('val9 (struct1)', f"{struct1.get('val0')}, {struct1.get('val1')}, {struct1.get('val2')}"),
        ('val10', info.get('val10')),
        ('val16', info.get('val16')),
        ('rent', f"mount {rent.get('mount_uid')}, val1 {rent.get('val1')}, val2 {rent.get('val2')}"),
        ('val19', info.get('val19')),
        ('val20', info.get('val20')),
        ('character parts_val0', c.get('parts_val0')),
        ('horse val0', h.get('val0')),
        ('horse val16', f"0x{h.get('val16', 0):08X}"),
        ('horse vals1 val1/5/9/12/14', f"{h.get('vals1_val1')}, {h.get('vals1_val5')}, "
                                       f"{h.get('vals1_val9')}, {h.get('vals1_val12')}, {h.get('vals1_val14')}"),
        ('horse vals1 spirit', h.get('vals1_spirit')),
        ('horse stopAmendsPoint', cond.get('stop_amends')),
        ('management val0', mgmt.get('val0')),
        ('guild val1/val2/val5/val6', f"{guild.get('val1')}, {guild.get('val2')}, "
                                      f"{guild.get('val5')}, {guild.get('val6')}"),
        ('pet member2', pet.get('member2')),
    ]
    for name, value in unknown:
        print(f"    {name + ':':<28} {value}")

    if 'parse_error' in info:
        print(f"\n  ⚠ Parse stopped: {info['parse_error']}")
    else:
        print(f"\n  ✓ Fully parsed ({info.get('bytes_parsed', '?')} bytes, "
              f"{info.get('bytes_remaining', '?')} remaining)")


# Server communication

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


# Auth

def auth_soa() -> tuple[str, str, int] | None:
    # Discord OAuth, the token is in the url it redirects you to
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


# Self tests

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

    # Class progress, checked with the Horse Profile in game
    for horse_class, class_exp, expected in [(17, 154179, 61.73), (24, 263921, 92.27), (30, 999999, 100.0)]:
        got = class_progress_percent(horse_class, class_exp)
        passed = abs(got - expected) < 0.01
        ok &= passed
        print(f"  {'✓' if passed else '✗'} class {horse_class} progress = {got:.2f}%")

    passed = ipv4_from_u32(0x054B9BED) == "5.75.155.237"
    ok &= passed
    print(f"  {'✓' if passed else '✗'} ranch address byte order")

    passed = online_players_from_notice("Players online: 228") == 228
    ok &= passed
    print(f"  {'✓' if passed else '✗'} online players from notice")

    # 2026-10-15 13:37 packed like util::DateTimeToAliciaTime does it
    packed = 2026 | (10 << 12) | (15 << 16) | (13 << 21) | (37 << 26)
    passed = alicia_time_text(packed) == "2026-10-15 13:37"
    ok &= passed
    print(f"  {'✓' if passed else '✗'} alicia date")

    # 2026-01-01 00:00:00 utc as a FILETIME
    filetime = (1767225600 + 11_644_473_600) * 10_000_000
    passed = filetime_to_text(filetime & 0xFFFFFFFF, filetime >> 32) == "2026-01-01 00:00:00 UTC"
    ok &= passed
    print(f"  {'✓' if passed else '✗'} server time")

    # Level 10 starts at 24600 exp and level 11 at 29000
    passed = abs(character_level_percent(10, 26800) - 50.0) < 0.01
    ok &= passed
    print(f"  {'✓' if passed else '✗'} character level progress")

    flags = bit_names((1 << 0) | (1 << 12) | (1 << 21), VISUAL_CLEANLINESS)
    passed = flags == ["BodySlightlyDirty", "ManeLightSparkles", "TailVeryDirty"]
    ok &= passed
    print(f"  {'✓' if passed else '✗'} horse cleanliness flags")

    print(f"\n  {'All tests passed.' if ok else 'SOME TESTS FAILED.'}\n")
    return ok


# Main

def arg_value(flag: str) -> str | None:
    if flag in sys.argv:
        index = sys.argv.index(flag)
        if index + 1 < len(sys.argv):
            return sys.argv[index + 1]
    return None


def main():
    skip_tests = "--skip-tests" in sys.argv
    json_path = arg_value("--json")
    server_repo = arg_value("--server-repo")
    item_names = load_item_names(server_repo) if server_repo else {}

    print()
    print("  ╔═════════════════════════════════════════════════╗")
    print("  ║         Story of Alicia - Protocol Client       ║")
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
        display_login_ok(result, server['name'], item_names)
        if json_path:
            with open(json_path, 'w', encoding='utf-8') as f:
                json.dump(result, f, ensure_ascii=False, indent=2)
            print(f"\n  Saved everything to {json_path}")
    elif result['status'] == 'rejected':
        print(f"\n  ✗ Login rejected: {result['reason']}")
    elif result['status'] == 'offline':
        print(f"\n  ✗ Server offline: {result.get('error', '?')}")
    else:
        print(f"\n  ✗ {result['status']}: {result}")

    print()


if __name__ == "__main__":
    main()