import struct
import sys
from io import BytesIO


def records(data):
    count = struct.unpack_from("<I", data)[0]
    for index in range(count):
        toolset, kind, rule, offset, size = struct.unpack_from(
            "<5I", data, 4 + index * 20)
        yield toolset, kind, rule, data[offset:offset + size]


def bytes_cpp(output, name, data):
    output.write("const unsigned char %s[] = {\n" % name)
    for offset in range(0, len(data), 16):
        output.write("    " + ", ".join(
            "0x%02x" % value for value in data[offset:offset + 16]) + ",\n")
    output.write("};\n")


def types_cpp(output_name, values):
    packfiles = [value for value in values if value[1] == 0]
    legacy = next(value[3] for value in values if value[1] == 1)
    payload = bytearray()
    offsets = {}
    table = []
    for toolset, _, rule, data in packfiles:
        header = struct.unpack_from("<13I", data)
        section_data = data[52:]
        if section_data not in offsets:
            offsets[section_data] = len(payload)
            payload.extend(section_data)
        table.append((toolset, rule) + header + (offsets[section_data],))

    with open(output_name, "w", newline="\n") as output:
        output.write("// This file has been automatically generated. Do not modify.\n")
        output.write("extern const unsigned char typeData[];\n\n")
        output.write("struct TypeEntry {\n"
                     "  uint8 toolset;\n  uint8 profile;\n"
                     "  uint32 contentsClassNameOffset;\n"
                     "  uint32 classLocal, classGlobal, classVirtual;\n"
                     "  uint32 classExports, classImports, classSize;\n"
                     "  uint32 typeLocal, typeGlobal, typeVirtual;\n"
                     "  uint32 typeExports, typeImports, typeSize;\n"
                     "  uint32 offset;\n"
                     "  const unsigned char *ClassData() const {\n"
                     "    return typeData + offset;\n"
                     "  }\n"
                     "  const unsigned char *TypeData() const {\n"
                     "    return ClassData() + classSize;\n"
                     "  }\n};\n\n")
        bytes_cpp(output, "typeData", payload)
        output.write("\nconst TypeEntry typeEntries[] = {\n")
        for entry in table:
            output.write("    {%s},\n" % ", ".join(map(str, entry)))
        output.write("};\n\n")
        bytes_cpp(output, "havok300Metadata", legacy)


def string(input_file):
    size = struct.unpack("<H", input_file.read(2))[0]
    return input_file.read(size).decode("utf-8")


def quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def tagfile_cpp(output_name, values):
    input_file = BytesIO(next(value[3] for value in values if value[1] == 2))
    _, _, member_count, template_count, interface_count, type_count = \
        struct.unpack("<8sIIIII", input_file.read(28))
    members = [(string(input_file),) + struct.unpack("<BHH", input_file.read(5))
               for _ in range(member_count)]
    templates = [(string(input_file),) + struct.unpack("<H", input_file.read(2))
                 for _ in range(template_count)]
    interfaces = [struct.unpack("<HB", input_file.read(3))
                  for _ in range(interface_count)]
    types = [(string(input_file),) +
             struct.unpack("<HB I HHHB II HHHHHH", input_file.read(34))
             for _ in range(type_count)]

    with open(output_name, "w", newline="\n") as output:
        output.write("// This file has been automatically generated. Do not modify.\n")
        output.write("const TagfileMember tagfileMembers[] = {\n")
        for name, flags, offset, type_index in members:
            output.write("    {%s, %u, %u, %u},\n" %
                         (quote(name), flags, offset, type_index))
        output.write("};\n\nconst TagfileTemplate tagfileTemplates[] = {\n")
        for name, value in templates:
            output.write("    {%s, %u},\n" % (quote(name), value))
        output.write("};\n\nconst TagfileInterface tagfileInterfaces[] = {\n")
        for type_index, flags in interfaces:
            output.write("    {%u, %u},\n" % (type_index, flags))
        output.write("};\n\nconst TagfileType tagfileTypes[] = {\n"
                     "    {nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},\n")
        for value in types:
            output.write("    {%s, %s},\n" %
                         (quote(value[0]), ", ".join(map(str, value[1:]))))
        output.write("};\n")


data = open(sys.argv[1], "rb").read()
values = list(records(data))
types_cpp(sys.argv[2], values)
tagfile_cpp(sys.argv[3], values)
