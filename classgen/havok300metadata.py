import sys


def main():
    data = open(sys.argv[1], "rb").read()
    with open(sys.argv[2], "w", newline="\n") as output:
        output.write("// This file has been automatically generated. Do not modify.\n")
        output.write("const unsigned char havok300Metadata[] = {\n")
        for offset in range(0, len(data), 16):
            values = ", ".join("0x%02x" % value for value in data[offset:offset + 16])
            output.write("    " + values + ",\n")
        output.write("};\n")


if __name__ == "__main__":
    main()
