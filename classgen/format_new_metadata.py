import struct
import sys


def read_string(input):
    size = struct.unpack("<H", input.read(2))[0]
    return input.read(size).decode("utf-8")


def cpp_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as input:
        magic, version, num_members, num_templates, num_interfaces, num_types = \
            struct.unpack("<8sIIIII", input.read(28))

        if magic != b"HK16META" or version != 1:
            sys.exit(1)

        members = [
            (read_string(input),) + struct.unpack("<BHH", input.read(5))
            for _ in range(num_members)
        ]
        templates = [
            (read_string(input),) + struct.unpack("<H", input.read(2))
            for _ in range(num_templates)
        ]
        interfaces = [
            struct.unpack("<HB", input.read(3))
            for _ in range(num_interfaces)
        ]
        types = [
            (read_string(input),) +
            struct.unpack("<HB I HHHB II HHHHHH", input.read(34))
            for _ in range(num_types)
        ]

    sys.stdout = open(sys.argv[2], "w", newline="\n")
    print("// This file has been automatically generated. Do not modify.")
    print("const TagfileMember tagfileMembers[] = {")
    for name, flags, offset, type in members:
        print("    {%s, %u, %u, %u}," %
              (cpp_string(name), flags, offset, type))
    print("};\n\nconst TagfileTemplate tagfileTemplates[] = {")
    for name, value in templates:
        print("    {%s, %u}," % (cpp_string(name), value))
    print("};\n\nconst TagfileInterface tagfileInterfaces[] = {")
    for type, flags in interfaces:
        print("    {%u, %u}," % (type, flags))
    print("};\n\nconst TagfileType tagfileTypes[] = {")
    print("    {nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},")
    for type in types:
        print("    {%s, %s}," %
              (cpp_string(type[0]), ", ".join(map(str, type[1:]))))
    print("};")
