import struct
import sys
import xml.etree.ElementTree as ET
from copy import deepcopy


TYPE_IDS = {
    "TYPE_VOID": 0, "TYPE_BOOL": 1, "TYPE_CHAR": 2,
    "TYPE_INT8": 3, "TYPE_UINT8": 4, "TYPE_INT16": 5,
    "TYPE_UINT16": 6, "TYPE_INT32": 7, "TYPE_UINT32": 8,
    "TYPE_INT64": 9, "TYPE_UINT64": 10, "TYPE_REAL": 11,
    "TYPE_VECTOR4": 12, "TYPE_QUATERNION": 13, "TYPE_MATRIX3": 14,
    "TYPE_ROTATION": 15, "TYPE_QSTRANSFORM": 16, "TYPE_MATRIX4": 17,
    "TYPE_TRANSFORM": 18, "TYPE_ZERO": 19, "TYPE_POINTER": 20,
    "TYPE_FUNCTIONPOINTER": 21, "TYPE_ARRAY": 22,
    "TYPE_INPLACEARRAY": 23, "TYPE_ENUM": 24, "TYPE_STRUCT": 25,
    "TYPE_SIMPLEARRAY": 26, "TYPE_HOMOGENEOUSARRAY": 27,
    "TYPE_VARIANT": 28, "TYPE_CSTRING": 29, "TYPE_ULONG": 30,
    "TYPE_FLAGS": 31, "TYPE_HALF": 32, "TYPE_STRINGPTR": 33,
    "TYPE_RELARRAY": 34,
}

BASE_CLASS_SIGNATURES = [
    ("hkClass", 0x14425e51),
    ("hkClassMember", 0x4a986551),
    ("hkClassEnum", 0x8a3609cf),
    ("hkClassEnumItem", 0xce6f8a6c),
    ("hkRootLevelContainer", 0x12a4e063),
    ("hkPhysicsData", 0xc2a461e4),
    ("hkPhysicsSystem", 0xe15f41a4),
    ("hkRigidBody", 0x33b07a1e),
    ("hkConvexTranslateShape", 0xd5ccc442),
    ("hkBoxShape", 0x9d45ad25),
    ("hkSphereMotion", 0xe6f30c22),
    ("hkSpatialRigidBodyDeactivator", 0x7aa84797),
]

SUPPLEMENTAL_CLASS_SIGNATURES = [
    [("hkConvexVerticesShape", 0x03363466)],
    [("hkCylinderShape", 0x6c787842)],
    [("hkListShape", 0x39c84054)],
]

SUPPLEMENTAL_CLASSES = {
    "hkConvexVerticesShape",
    "hkConvexVerticesShapeFourVectors",
    "hkCylinderShape",
    "hkListShape",
    "hkListShapeChildInfo",
    "hkShapeCollection",
}


def align(data, alignment=16, value=0xff):
    data.extend(bytes([value]) * ((-len(data)) & (alignment - 1)))


def params(node):
    return {child.get("name"): child for child in node.findall("hkparam")}


def text(node, name, default=""):
    value = params(node).get(name)
    return default if value is None else (value.text or "").strip()


def pack_into(data, endian, fmt, offset, *values):
    struct.pack_into(endian + fmt, data, offset, *values)


def build_class_names(signatures, endian):
    data = bytearray()
    offsets = {}
    for name, signature in signatures:
        data.extend(struct.pack(endian + "I", signature))
        data.append(9)
        offsets[name] = len(data)
        data.extend(name.encode("ascii") + b"\0")
    align(data)
    return data, offsets


def build_types(objects, class_name_offsets, endian):
    data = bytearray()
    object_offsets = {}
    local_fixups = []
    global_fixups = []
    virtual_fixups = []

    for obj in objects:
        align(data)
        object_offsets[obj.get("name")] = len(data)
        data.extend(bytes(48 if obj.get("class") == "hkClass" else 20))

    def add_string(value):
        offset = len(data)
        data.extend(value.encode("ascii") + b"\0")
        return offset

    def add_pointer(pointer, destination):
        local_fixups.append((pointer, destination))

    def add_object_pointer(pointer, target):
        if target and target != "null":
            global_fixups.append((pointer, 1, object_offsets[target]))

    def fill_enum(enum_node, offset):
        enum_params = params(enum_node)
        enum_name = (enum_params["name"].text or "").strip()
        add_pointer(offset, add_string(enum_name))
        items = enum_params.get("items")
        item_nodes = [] if items is None else items.findall("hkobject")
        pack_into(data, endian, "I", offset + 8, len(item_nodes))
        if item_nodes:
            align(data)
            item_offset = len(data)
            data.extend(bytes(8 * len(item_nodes)))
            add_pointer(offset + 4, item_offset)
            for index, item in enumerate(item_nodes):
                item_params = params(item)
                record = item_offset + index * 8
                pack_into(data, endian, "i", record,
                          int((item_params["value"].text or "0").strip(), 0))
                item_name = (item_params["name"].text or "").strip()
                add_pointer(record + 4, add_string(item_name))

    for obj in objects:
        offset = object_offsets[obj.get("name")]
        obj_class = obj.get("class")
        obj_params = params(obj)
        if obj_class == "hkClassEnum":
            virtual_fixups.append(
                (offset, 0, class_name_offsets["hkClassEnum"]))
            fill_enum(obj, offset)
            continue

        virtual_fixups.append((offset, 0, class_name_offsets["hkClass"]))
        name = (obj_params["name"].text or "").strip()
        add_pointer(offset, add_string(name))
        add_object_pointer(offset + 4, text(obj, "parent", "null"))
        pack_into(data, endian, "I", offset + 8,
                  int(text(obj, "objectSize", "0"), 0))
        pack_into(data, endian, "I", offset + 12,
                  int(text(obj, "numImplementedInterfaces", "0"), 0))

        enums = obj_params.get("declaredEnums")
        enum_nodes = [] if enums is None else enums.findall("hkobject")
        pack_into(data, endian, "I", offset + 20, len(enum_nodes))
        if enum_nodes:
            align(data)
            enum_offset = len(data)
            data.extend(bytes(20 * len(enum_nodes)))
            add_pointer(offset + 16, enum_offset)
            for index, enum_node in enumerate(enum_nodes):
                fill_enum(enum_node, enum_offset + index * 20)

        members = obj_params.get("declaredMembers")
        member_nodes = [] if members is None else members.findall("hkobject")
        pack_into(data, endian, "I", offset + 28, len(member_nodes))
        if member_nodes:
            align(data)
            member_offset = len(data)
            data.extend(bytes(24 * len(member_nodes)))
            add_pointer(offset + 24, member_offset)
            for index, member in enumerate(member_nodes):
                member_params = params(member)
                record = member_offset + index * 24
                member_name = (member_params["name"].text or "").strip()
                add_pointer(record, add_string(member_name))
                add_object_pointer(record + 4, text(member, "class", "null"))
                add_object_pointer(record + 8, text(member, "enum", "null"))
                data[record + 12] = TYPE_IDS[text(member, "type", "TYPE_VOID")]
                data[record + 13] = TYPE_IDS[text(member, "subtype", "TYPE_VOID")]
                pack_into(data, endian, "H", record + 14,
                          int(text(member, "cArraySize", "0"), 0))
                flags = 0x400 if text(member, "flags", "0") == "SERIALIZE_IGNORED" else 0
                pack_into(data, endian, "H", record + 16, flags)
                pack_into(data, endian, "H", record + 18,
                          int(text(member, "offset", "0"), 0))

        pack_into(data, endian, "I", offset + 40,
                  int(text(obj, "flags", "0").split("|")[0] or "0", 0)
                  if text(obj, "flags", "0").split("|")[0].isdigit() else 0)
        pack_into(data, endian, "i", offset + 44,
                  int(text(obj, "describedVersion", "0"), 0))

    align(data)
    local_fixups.sort()
    global_fixups.sort()
    virtual_fixups.sort()
    return data, local_fixups, global_fixups, virtual_fixups, object_offsets


def validate_reference(objects, file_name):
    source = open(file_name, "rb").read()
    sections = [struct.unpack_from(">7I", source, 84 + index * 48)
                for index in range(3)]
    class_start, _, _, _, _, _, class_size = sections[0]
    type_start, local_offset, global_offset, virtual_offset, exports_offset, _, _ = sections[1]
    class_data = source[class_start:class_start + class_size]
    type_data = source[type_start:type_start + local_offset]

    class_names = {}
    offset = 0
    while offset + 5 <= len(class_data) and class_data[offset + 4] == 9:
        end = class_data.index(0, offset + 5)
        class_names[offset + 5] = class_data[offset + 5:end].decode("ascii")
        offset = end + 1

    local_fixups = {}
    for offset in range(type_start + local_offset,
                        type_start + global_offset, 8):
        pointer, destination = struct.unpack_from(">ii", source, offset)
        if pointer >= 0:
            local_fixups[pointer] = destination
    global_fixups = {}
    for offset in range(type_start + global_offset,
                        type_start + virtual_offset - 11, 12):
        pointer, section, destination = struct.unpack_from(">iii", source, offset)
        if pointer >= 0 and section == 1:
            global_fixups[pointer] = destination

    xml_classes = {text(obj, "name"): obj for obj in objects
                   if obj.get("class") == "hkClass"}
    checked_classes = 0
    checked_members = 0
    checked_enums = 0

    def string_at(destination):
        end = type_data.index(0, destination)
        return type_data[destination:end].decode("ascii")

    for offset in range(type_start + virtual_offset,
                        type_start + exports_offset, 12):
        object_offset, section, name_offset = struct.unpack_from(">iii", source, offset)
        if object_offset < 0 or section != 0:
            continue
        object_kind = class_names[name_offset]
        object_name = string_at(local_fixups[object_offset])
        if object_kind == "hkClassEnum":
            if struct.unpack_from(">I", type_data, object_offset + 8)[0] == 0:
                raise RuntimeError("empty reflected enum " + object_name)
            checked_enums += 1
            continue
        if object_kind != "hkClass" or object_name not in xml_classes:
            continue

        obj = xml_classes[object_name]
        obj_params = params(obj)
        members = obj_params["declaredMembers"].findall("hkobject")
        values = struct.unpack_from(">6I", type_data, object_offset + 8)
        if values[0] != int(text(obj, "objectSize"), 0):
            raise RuntimeError("object size mismatch for " + object_name)
        if values[1] != int(text(obj, "numImplementedInterfaces"), 0):
            raise RuntimeError("interface count mismatch for " + object_name)
        if values[3] != len(obj_params["declaredEnums"].findall("hkobject")):
            raise RuntimeError("enum count mismatch for " + object_name)
        if values[5] != len(members):
            raise RuntimeError("member count mismatch for " + object_name)

        parent = text(obj, "parent", "null")
        if parent != "null":
            binary_parent = global_fixups[object_offset + 4]
            expected_parent = text(next(item for item in objects
                                        if item.get("name") == parent), "name")
            if string_at(local_fixups[binary_parent]) != expected_parent:
                raise RuntimeError("parent mismatch for " + object_name)

        if members:
            member_offset = local_fixups[object_offset + 24]
            for index, member in enumerate(members):
                record = member_offset + index * 24
                member_params = params(member)
                member_name = (member_params["name"].text or "").strip()
                if string_at(local_fixups[record]) != member_name:
                    raise RuntimeError("member name mismatch for " + object_name)
                if type_data[record + 12] != TYPE_IDS[text(member, "type")]:
                    raise RuntimeError("member type mismatch for " + object_name)
                if type_data[record + 13] != TYPE_IDS[text(member, "subtype")]:
                    raise RuntimeError("member subtype mismatch for " + object_name)
                if struct.unpack_from(">H", type_data, record + 18)[0] != \
                        int(text(member, "offset"), 0):
                    raise RuntimeError("member offset mismatch for " + object_name)
                expected_flags = 0x400 if text(member, "flags", "0") == \
                    "SERIALIZE_IGNORED" else 0
                if struct.unpack_from(">H", type_data, record + 16)[0] != \
                        expected_flags:
                    raise RuntimeError("member flags mismatch for " + object_name)
                checked_members += 1
        checked_classes += 1

    if checked_classes < 30 or checked_members < 100 or checked_enums < 5:
        raise RuntimeError("incomplete 3.3 metadata verification")


def validate_supplemental_layouts(objects, file_name, class_names):
    source = open(file_name, "rb").read()
    endian = "<" if source[17] else ">"
    num_sections = struct.unpack_from(endian + "i", source, 20)[0]
    sections = []
    for index in range(num_sections):
        offset = 64 + index * 48
        tag = source[offset:offset + 20].split(b"\0", 1)[0].decode("ascii")
        sections.append((tag,) + struct.unpack_from(endian + "7I", source,
                                                     offset + 20))

    type_section = next(section for section in sections
                        if section[0] == "__types__")
    class_index = next(section for section in sections
                       if section[0] == "__classindex__")
    _, type_start, local_offset, global_offset, virtual_offset, _, _, _ = \
        type_section
    type_data = source[type_start:type_start + local_offset]

    local_fixups = {}
    for offset in range(type_start + local_offset,
                        type_start + global_offset, 8):
        pointer, destination = struct.unpack_from(endian + "ii", source,
                                                   offset)
        if pointer >= 0:
            local_fixups[pointer] = destination

    global_fixups = {}
    for offset in range(type_start + global_offset,
                        type_start + virtual_offset - 11, 12):
        pointer, section, destination = struct.unpack_from(endian + "iii",
                                                            source, offset)
        if pointer >= 0 and section == sections.index(type_section):
            global_fixups[pointer] = destination

    def string_at(destination):
        end = type_data.index(0, destination)
        return type_data[destination:end].decode("ascii")

    class_offsets = set()
    _, index_start, index_size, _, _, _, _, _ = class_index
    for offset in range(index_start, index_start + index_size, 16):
        source_section, begin, destination_section, _ = \
            struct.unpack_from(endian + "4I", source, offset)
        if source_section == sections.index(type_section) and \
                destination_section == source_section:
            class_offsets.add(begin)
    class_offsets.update(global_fixups.values())
    class_offsets.update(local_fixups)

    binary_classes = {}
    for offset in class_offsets:
        if offset not in local_fixups or offset + 48 > len(type_data):
            continue
        name = string_at(local_fixups[offset])
        if not name.startswith("hk"):
            continue
        object_size, _, _, enum_count, _, member_count = \
            struct.unpack_from(endian + "6I", type_data, offset + 8)
        if object_size > 0xffff or enum_count > 0xff or member_count > 0xff:
            continue
        if member_count and offset + 24 not in local_fixups:
            continue

        members = []
        if member_count:
            member_offset = local_fixups[offset + 24]
            for member_index in range(member_count):
                record = member_offset + member_index * 20
                member_name = string_at(local_fixups[record])
                member_type = type_data[record + 12]
                member_subtype = type_data[record + 13]
                c_array_size, flags, member_data_offset = struct.unpack_from(
                    endian + "HHH", type_data, record + 14)
                members.append((member_name, member_type, member_subtype,
                                c_array_size, flags, member_data_offset))

        parent = global_fixups.get(offset + 4)
        parent_name = None if parent is None else \
            string_at(local_fixups[parent])
        binary_classes[name] = (object_size, parent_name, members)

    xml_classes = {text(obj, "name"): obj for obj in objects
                   if obj.get("class") == "hkClass"}
    for class_name in class_names:
        obj = xml_classes[class_name]
        object_size, parent_name, binary_members = binary_classes[class_name]
        if object_size != int(text(obj, "objectSize"), 0):
            raise RuntimeError("object size mismatch for " + class_name)
        expected_parent = text(obj, "parent", "null")
        if (None if expected_parent == "null" else expected_parent) != \
                parent_name:
            raise RuntimeError("parent mismatch for " + class_name)

        member_node = params(obj)["declaredMembers"]
        xml_members = member_node.findall("hkobject")
        if len(xml_members) != len(binary_members):
            raise RuntimeError("member count mismatch for " + class_name)
        for member, binary_member in zip(xml_members, binary_members):
            member_params = params(member)
            expected = (
                (member_params["name"].text or "").strip(),
                TYPE_IDS[text(member, "type")],
                TYPE_IDS[text(member, "subtype")],
                int(text(member, "cArraySize", "0"), 0),
                0,
                int(text(member, "offset", "0"), 0),
            )
            if expected != binary_member:
                raise RuntimeError("member mismatch for " + class_name +
                                   "." + expected[0])


def normalized_objects(file_name, supplemental=False):
    root = ET.parse(file_name).getroot()
    section = root.find("hksection[@name='__types__']")
    source_objects = [obj for obj in section.findall("hkobject")
                      if obj.get("class") in ("hkClass", "hkClassEnum")]
    source_names = {obj.get("name"): text(obj, "name")
                    for obj in source_objects}
    objects = []
    for source in source_objects:
        name = text(source, "name")
        if supplemental and name not in SUPPLEMENTAL_CLASSES:
            continue
        obj = deepcopy(source)
        obj.set("name", name)
        for value in obj.findall(".//hkparam"):
            if value.get("name") not in ("parent", "class", "enum"):
                continue
            reference = (value.text or "").strip()
            if reference in source_names:
                value.text = source_names[reference]
        objects.append(obj)
    return objects


def build_payload(objects, signatures):
    class_be, names = build_class_names(signatures, ">")
    class_le, names_le = build_class_names(signatures, "<")
    type_be, local_fixups, global_fixups, virtual_fixups, object_offsets = \
        build_types(objects, names, ">")
    type_le, local_le, global_le, virtual_le, object_offsets_le = \
        build_types(objects, names_le, "<")
    if (local_fixups != local_le or global_fixups != global_le or
            virtual_fixups != virtual_le or object_offsets != object_offsets_le):
        sys.exit(1)

    payload = bytearray(struct.pack("<6I", len(class_be), len(type_be),
                                    len(local_fixups), len(global_fixups),
                                    len(virtual_fixups),
                                    names["hkRootLevelContainer"]))
    payload.extend(class_be)
    payload.extend(type_be)
    payload.extend(class_le)
    payload.extend(type_le)
    for item in local_fixups:
        payload.extend(struct.pack("<ii", *item))
    for item in global_fixups:
        payload.extend(struct.pack("<iii", *item))
    for item in virtual_fixups:
        payload.extend(struct.pack("<iii", *item))
    return payload


def main():
    base_objects = normalized_objects(sys.argv[1])
    validate_reference(base_objects, sys.argv[5])

    supplemental_groups = []
    for source_name in sys.argv[2:5]:
        group = []
        for obj in normalized_objects(source_name, True):
            group.append(obj)
        supplemental_groups.append(group)

    extended_objects = list(base_objects)
    known_names = {obj.get("name") for obj in extended_objects}
    for group in supplemental_groups:
        for obj in group:
            if obj.get("name") not in known_names:
                extended_objects.append(obj)
                known_names.add(obj.get("name"))
    validate_reference(extended_objects, sys.argv[5])
    validate_supplemental_layouts(
        extended_objects, sys.argv[6], {"hkCylinderShape"})
    validate_supplemental_layouts(
        extended_objects, sys.argv[7],
        {"hkListShape", "hkListShapeChildInfo", "hkShapeCollection"})

    payloads = []
    for profile in range(8):
        objects = list(base_objects)
        signatures = list(BASE_CLASS_SIGNATURES)
        profile_names = {obj.get("name") for obj in objects}
        for bit, group in enumerate(supplemental_groups):
            if not profile & (1 << bit):
                continue
            for obj in group:
                if obj.get("name") not in profile_names:
                    objects.append(obj)
                    profile_names.add(obj.get("name"))
            signatures.extend(SUPPLEMENTAL_CLASS_SIGNATURES[bit])
        payloads.append(build_payload(objects, signatures))

    with open(sys.argv[8], "wb") as output:
        output.write(struct.pack("<8I", *(len(payload) for payload in payloads)))
        for payload in payloads:
            output.write(payload)


if __name__ == "__main__":
    main()
