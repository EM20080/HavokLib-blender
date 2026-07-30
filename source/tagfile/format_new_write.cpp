/*  Havok Format Library
    Copyright(C) 2016-2026 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "../fixups.hpp"
#include "hklib/hk_packfile.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/io/binwritter.hpp"
#include <algorithm>
#include <unordered_map>
namespace {

    struct TagfileMember {
        const char* name;
        uint8 flags;
        uint16 offset;
        uint16 type;
    };

    struct TagfileTemplate {
        const char* name;
        uint16 value;
    };

    struct TagfileInterface {
        uint16 type;
        uint8 flags;
    };

    struct TagfileType {
        const char* name;
        uint16 parent;
        uint8 flags;
        uint32 subTypeFlags;
        uint16 pointer;
        uint16 version;
        uint16 byteSize;
        uint8 alignment;
        uint32 abstractValue;
        uint32 hash;
        uint16 memberBegin;
        uint16 numMembers;
        uint16 templateBegin;
        uint16 numTemplates;
        uint16 interfaceBegin;
        uint16 numInterfaces;
    };

#include "metadata_new.inl"

    enum TagfileSubType : uint8 {
        TagfileString = 3,
        TagfilePointer = 6,
        TagfileClass = 7,
        TagfileArray = 8,
        TagfileTuple = 0x28,
    };

    struct ReflectedObject {
        const IhkVirtualClass* instance;
        uint16 type;
        std::string data;
        std::vector<hkFixup> fixups;
    };

    class MemoryStreamBuffer : public std::streambuf {
    public:
        std::string TakeData() { return std::move(data); }

    private:
        pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
            std::ios_base::openmode) override {
            off_type base = 0;
            if (direction == std::ios_base::cur) {
                base = static_cast<off_type>(position);
            }
            else if (direction == std::ios_base::end) {
                base = static_cast<off_type>(data.size());
            }
            return seekpos(pos_type(base + offset), std::ios_base::out);
        }

        pos_type seekpos(pos_type offset, std::ios_base::openmode) override {
            const off_type value = offset;
            if (value < 0) {
                return pos_type(off_type(-1));
            }
            position = static_cast<size_t>(value);
            if (position > data.size()) {
                data.resize(position);
            }
            return offset;
        }

        int_type overflow(int_type value) override {
            if (traits_type::eq_int_type(value, traits_type::eof())) {
                return traits_type::not_eof(value);
            }
            const char character = traits_type::to_char_type(value);
            xsputn(&character, 1);
            return value;
        }

        std::streamsize xsputn(const char* value, std::streamsize size) override {
            if (size <= 0) {
                return 0;
            }
            const size_t end = position + static_cast<size_t>(size);
            if (end > data.size()) {
                data.resize(end);
            }
            std::copy_n(value, static_cast<size_t>(size), data.data() + position);
            position = end;
            return size;
        }

        std::string data;
        size_t position{};
    };

    struct ObjectAddress {
        const ReflectedObject* object;
        uint32 offset;

        bool operator==(const ObjectAddress& other) const {
            return object == other.object && offset == other.offset;
        }
    };

    struct ObjectAddressHash {
        size_t operator()(const ObjectAddress& value) const {
            return reinterpret_cast<size_t>(value.object) ^ value.offset;
        }
    };

    struct TagItem {
        uint16 type;
        const ReflectedObject* object;
        uint32 nativeOffset;
        uint32 offset;
        uint32 count;
        uint32 kind;
    };

    struct PortableLayout {
        uint32 size;
        uint32 alignment;
        uint8 state;
    };

    struct Chunk {
        size_t offset;
        uint32 flags;
    };

    enum : uint32 {
        BranchChunk = 0,
        LeafChunk = 0x40000000,
        PointerItem = 0x10000000,
        ValueItem = 0x20000000,
    };

    void WriteBigEndian(BinWritterRef_e wr, uint32 value) {
        const char data[] = { static_cast<char>(value >> 24),
                             static_cast<char>(value >> 16),
                             static_cast<char>(value >> 8), static_cast<char>(value) };
        wr.WriteBuffer(data, sizeof(data));
    }

    void WriteLittleEndian(BinWritterRef_e wr, uint32 value) {
        const char data[] = { static_cast<char>(value), static_cast<char>(value >> 8),
                             static_cast<char>(value >> 16),
                             static_cast<char>(value >> 24) };
        wr.WriteBuffer(data, sizeof(data));
    }

    void Pad(BinWritterRef_e wr, size_t alignment) {
        const size_t amount = (alignment - wr.Tell() % alignment) % alignment;
        wr.Skip(amount);
    }

    void Pad(std::string& data, size_t alignment) {
        data.resize((data.size() + alignment - 1) & ~(alignment - 1));
    }

    Chunk BeginChunk(BinWritterRef_e wr, const char* tag, uint32 flags) {
        Chunk chunk{ wr.Tell(), flags };
        wr.Skip(4);
        wr.WriteBuffer(tag, 4);
        return chunk;
    }

    void EndChunk(BinWritterRef_e wr, Chunk chunk) {
        Pad(wr, 4);
        const size_t end = wr.Tell();
        wr.Seek(chunk.offset);
        WriteBigEndian(wr, static_cast<uint32>(end - chunk.offset) | chunk.flags);
        wr.Seek(end);
    }

    void WritePacked(BinWritterRef_e wr, uint32 value) {
        if (value < 0x80) {
            wr.Write<uint8>(static_cast<uint8>(value));
        }
        else if (value < 0x4000) {
            value |= 0x8000;
            const char data[] = { static_cast<char>(value >> 8),
                                 static_cast<char>(value) };
            wr.WriteBuffer(data, sizeof(data));
        }
        else if (value < 0x200000) {
            wr.Write<uint8>(static_cast<uint8>((value >> 16) | 0xc0));
            const char data[] = { static_cast<char>(value >> 8),
                                 static_cast<char>(value) };
            wr.WriteBuffer(data, sizeof(data));
        }
        else {
            WriteBigEndian(wr, value | 0xe0000000);
        }
    }

    uint16 FindType(std::string_view name) {
        for (uint16 i = 1; i < std::size(tagfileTypes); i++) {
            if (name == tagfileTypes[i].name) {
                return i;
            }
        }
        return 0;
    }

    uint16 SuperType(uint16 type) {
        while (type && !(tagfileTypes[type].flags & 1)) {
            type = tagfileTypes[type].parent;
        }
        return type;
    }

    uint8 SubType(uint16 type) {
        type = SuperType(type);
        return type ? static_cast<uint8>(tagfileTypes[type].subTypeFlags & 0x7f) : 0;
    }

    uint32 ReadUint32(const ReflectedObject& object, uint32 offset) {
        uint32 value = 0;
        if (offset + sizeof(value) <= object.data.size()) {
            std::copy_n(object.data.data() + offset, sizeof(value),
                reinterpret_cast<char*>(&value));
        }
        return value;
    }

    void WriteUint32(std::string& data, uint32 offset, uint32 value) {
        if (offset + sizeof(value) <= data.size()) {
            std::copy_n(reinterpret_cast<const char*>(&value), sizeof(value),
                data.data() + offset);
        }
    }

    const char* SdkVersion(hkToolset toolset) {
        switch (toolset) {
        case HK2015_1:
            return "20150100";
        case HK2015_2:
            return "20150200";
        case HK2016_1:
            return "20160100";
        case HK2016_2:
            return "20160200";
        default:
            return nullptr;
        }
    }

    class TagWriter {
    public:
        TagWriter(BinWritterRef_e writer, const IhkPackFile::VirtualClasses& classes,
            hkToolset toolset, const char* sdkVersion)
            : wr(writer), classes(classes), toolset(toolset), sdkVersion(sdkVersion),
            layouts(std::size(tagfileTypes)),
            memberOffsets(std::size(tagfileMembers)),
            metadataToType(std::size(tagfileTypes)),
            scannedTypes(std::size(tagfileTypes)) {
        }

        void Write() {
            ReflectClasses();
            BuildLayouts();
            BuildItems();
            BuildData();

            const Chunk root = BeginChunk(wr, "TAG0", BranchChunk);
            const Chunk sdk = BeginChunk(wr, "SDKV", LeafChunk);
            wr.WriteBuffer(sdkVersion, 8);
            EndChunk(wr, sdk);

            const Chunk dataChunk = BeginChunk(wr, "DATA", LeafChunk);
            wr.WriteBuffer(data.data(), data.size());
            EndChunk(wr, dataChunk);

            WriteTypes();
            WriteIndex();
            EndChunk(wr, root);
        }

    private:
        void ReflectClass(const IhkVirtualClass* instance) {
            const auto* instanceClass = dynamic_cast<const hkVirtualClass*>(instance);
            if (!instanceClass) {
                return;
            }
            const std::string_view className = instanceClass->GetClassName(toolset);
            uint16 type = FindType(className);
            if (!type &&
                className == "hkpStorageExtendedMeshShapeMeshSubpartStorage")
                type = FindType(
                    "hkpStorageExtendedMeshShape::MeshSubpartStorage");
            if (!type &&
                className == "hkpStorageExtendedMeshShapeShapeSubpartStorage")
                type = FindType(
                    "hkpStorageExtendedMeshShape::ShapeSubpartStorage");
            if (!type) {
                return;
            }

            std::unique_ptr<IhkVirtualClass> reflected(
                hkVirtualClass::Create(
                    JenHash(className),
                    { className.starts_with("hkp")
                          ? instanceClass->rule.version
                          : toolset,
                      0, 1 }));
            auto* reflectedClass = dynamic_cast<hkVirtualClass*>(reflected.get());
            if (!reflectedClass) {
                return;
            }

            reflectedClass->Reflect(instance);
            MemoryStreamBuffer buffer;
            std::ostream stream(&buffer);
            BinWritterRef_e objectWriter(stream);
            hkFixups fixups;
            reflectedClass->Save(objectWriter, fixups);
            objects.push_back(
                { instance, type, buffer.TakeData(), std::move(fixups.locals) });
        }
        void ReflectClasses() {
            for (const auto& instance : classes) {
                const auto* instanceClass =
                    dynamic_cast<const hkVirtualClass*>(instance.get());
                if (instanceClass &&
                    instanceClass->GetClassName(HK2016_1) == "hkRootLevelContainer") {
                    ReflectClass(instance.get());
                }
            }
            for (const auto& instance : classes) {
                const auto* instanceClass =
                    dynamic_cast<const hkVirtualClass*>(instance.get());
                if (!instanceClass ||
                    instanceClass->GetClassName(HK2016_1) != "hkRootLevelContainer") {
                    ReflectClass(instance.get());
                }
            }

            for (const ReflectedObject& object : objects) {
                for (const hkFixup& fixup : object.fixups) {
                    fixupByAddress[{&object, static_cast<uint32>(fixup.strOffset)}] =
                        &fixup;
                }
            }
        }

        const PortableLayout& BuildLayout(uint16 type) {
            PortableLayout& layout = layouts[type];
            if (layout.state == 2 || layout.state == 1) {
                return layout;
            }

            layout.state = 1;
            const TagfileType& value = tagfileTypes[type];
            const uint16 base = SuperType(type);
            const uint8 subType = SubType(base);
            const std::string_view name = value.name;
            const auto alignUp = [](uint32 size, uint32 alignment) {
                return (size + alignment - 1) & ~(alignment - 1);
                };

            layout.size = value.byteSize;
            layout.alignment = value.alignment;
            if (subType == TagfileString || subType == TagfilePointer ||
                subType == TagfileArray) {
                layout.size = layout.alignment = 4;
                for (uint16 i = 0; i < value.numMembers; i++) {
                    const uint16 memberIndex = value.memberBegin + i;
                    memberOffsets[memberIndex] = 0;
                }
            }
            else if (subType == TagfileTuple) {
                const TagfileType& tuple = tagfileTypes[base];
                const PortableLayout& element = BuildLayout(tuple.pointer);
                layout.size = element.size * (tuple.subTypeFlags >> 8);
                layout.alignment = element.alignment;
            }
            else if (subType == TagfileClass) {
                uint32 current = 0;
                layout.alignment = 1;
                if (value.parent) {
                    const PortableLayout& parent = BuildLayout(value.parent);
                    current = parent.size;
                    layout.alignment = parent.alignment;
                }
                for (uint16 i = 0; i < value.numMembers; i++) {
                    const uint16 memberIndex = value.memberBegin + i;
                    const TagfileMember& member = tagfileMembers[memberIndex];
                    if (member.flags & 1) {
                        continue;
                    }
                    const PortableLayout& memberLayout = BuildLayout(member.type);
                    current = alignUp(
                        current, std::max<uint32>(memberLayout.alignment, 1));
                    memberOffsets[memberIndex] = current;
                    current += memberLayout.size;
                    layout.alignment =
                        std::max(layout.alignment, memberLayout.alignment);
                }
                layout.size =
                    alignUp(current, std::max<uint32>(layout.alignment, 1));
            }
            if (!layout.size && base && base != type) {
                const PortableLayout& parent = BuildLayout(base);
                layout.size = parent.size;
                layout.alignment = parent.alignment;
            }

            if (name == "hkVector4" || name == "hkVector4f" ||
                name == "hkQuaternion" || name == "hkQuaternionf" ||
                name == "hkQsTransform" || name == "hkQsTransformf" ||
                name == "hkTransform" || name == "hkTransformf") {
                layout.alignment = std::max<uint32>(layout.alignment, 16);
                layout.size = alignUp(layout.size, layout.alignment);
            }

            layout.state = 2;
            return layout;
        }

        void BuildLayouts() {
            for (uint16 i = 1; i < std::size(tagfileTypes); i++) {
                BuildLayout(i);
            }
        }

        uint32 NativeSize(uint16 type, const ReflectedObject& object) const {
            const std::string_view objectType = tagfileTypes[object.type].name;
            const uint8 subType = SubType(type);
            if (objectType.starts_with("hkp") &&
                (subType == TagfileString || subType == TagfilePointer)) {
                return 4;
            }
            if ((objectType == "hkpStaticCompoundShape" ||
                objectType == "hkpBvCompressedMeshShape") &&
                std::string_view(tagfileTypes[type].name) ==
                "hkpStaticCompoundShape::Instance") {
                return 64;
            }
            const uint16 base = SuperType(type);
            return tagfileTypes[type].byteSize
                ? tagfileTypes[type].byteSize
                : (base ? tagfileTypes[base].byteSize : 0);
        }

        void ScanType(uint16 type) {
            if (!type || scannedTypes[type]) {
                return;
            }

            scannedTypes[type] = 1;
            metadataToType[type] = static_cast<uint16>(types.size());
            types.push_back(type);
            const TagfileType& value = tagfileTypes[type];

            for (uint16 i = 0; i < value.numTemplates; i++) {
                const TagfileTemplate& item = tagfileTemplates[value.templateBegin + i];
                if (item.name[0] == 't') {
                    ScanType(item.value);
                }
            }
            ScanType(value.parent);
            ScanType(value.pointer);
            for (uint16 i = 0; i < value.numMembers; i++) {
                ScanType(tagfileMembers[value.memberBegin + i].type);
            }
            for (uint16 i = 0; i < value.numInterfaces; i++) {
                ScanType(tagfileInterfaces[value.interfaceBegin + i].type);
            }
        }

        void BuildItems() {
            items.push_back({});
            types.push_back(0);

            if (!objects.empty()) {
                AddObject(objects.front().instance);
            }

            for (size_t item = 1; item < items.size(); item++) {
                const TagItem value = items[item];
                const uint32 size = NativeSize(value.type, *value.object);
                if (!size) {
                    continue;
                }
                for (uint32 i = 0; i < value.count; i++) {
                    const uint32 offset = value.nativeOffset + i * size;
                    ProcessValue(value.type, *value.object, offset, false);
                    ProcessValue(value.type, *value.object, offset, true);
                }
            }
        }
        uint32 AddObject(const IhkVirtualClass* instance) {
            const auto foundItem = objectItems.find(instance);
            if (foundItem != objectItems.end()) {
                return foundItem->second;
            }
            for (const ReflectedObject& object : objects) {
                if (object.instance == instance) {
                    const uint32 item = static_cast<uint32>(items.size());
                    items.push_back({ object.type, &object, 0, 0, 1, PointerItem });
                    objectItems[instance] = item;
                    destinationItems[{&object, 0}] = item;
                    ScanType(object.type);
                    return item;
                }
            }
            return 0;
        }

        void ProcessClass(uint16 type, const ReflectedObject& object, uint32 offset,
            bool pointers) {
            const TagfileType& value = tagfileTypes[type];
            if (value.parent) {
                ProcessClass(value.parent, object, offset, pointers);
            }
            for (uint16 i = 0; i < value.numMembers; i++) {
                const TagfileMember& member = tagfileMembers[value.memberBegin + i];
                if (!(member.flags & 1) &&
                    !(std::string_view(tagfileTypes[object.type].name).starts_with(
                        "hkp") &&
                      std::string_view(value.name) == "hkReferencedObject" &&
                      std::string_view(member.name) == "propertyBag")) {
                    ProcessValue(member.type, object,
                        offset + NativeMemberOffset(type, member), pointers);
                }
            }
        }

        void ProcessValue(uint16 type, const ReflectedObject& object, uint32 offset,
            bool pointers) {
            const uint16 base = SuperType(type);
            if (!base || offset >= object.data.size()) {
                return;
            }

            const uint8 subType = SubType(base);
            if (subType == TagfileString || subType == TagfilePointer ||
                subType == TagfileArray) {
                if ((subType == TagfilePointer) == pointers) {
                    ProcessReference(base, object, offset);
                }
            }
            else if (subType == TagfileClass) {
                ProcessClass(type, object, offset, pointers);
            }
            else if (subType == TagfileTuple) {
                const TagfileType& tuple = tagfileTypes[base];
                const uint32 size = NativeSize(tuple.pointer, object);
                if (!size) {
                    return;
                }
                const uint32 count = tuple.subTypeFlags >> 8;
                for (uint32 i = 0; i < count; i++) {
                    ProcessValue(tuple.pointer, object, offset + i * size, pointers);
                }
            }
        }

        void ProcessReference(uint16 base, const ReflectedObject& object,
            uint32 offset) {
            const ObjectAddress address{ &object, offset };
            const auto foundFixup = fixupByAddress.find(address);
            if (foundFixup == fixupByAddress.end()) {
                return;
            }

            const hkFixup& fixup = *foundFixup->second;
            uint32 item = 0;
            const uint8 subType = SubType(base);

            if (subType == TagfilePointer) {
                if (fixup.destClass) {
                    item = AddObject(fixup.destClass);
                }
            }
            else if (fixup.destination != static_cast<size_t>(-1) &&
                fixup.destination < object.data.size()) {
                const ObjectAddress destination{ &object,
                                                static_cast<uint32>(fixup.destination) };
                const auto foundItem = destinationItems.find(destination);
                if (foundItem != destinationItems.end()) {
                    item = foundItem->second;
                }
                else if (subType == TagfileString) {
                    uint32 count = 1;
                    while (destination.offset + count <= object.data.size() &&
                        object.data[destination.offset + count - 1]) {
                        count++;
                    }
                    const uint16 charType = FindType("char");
                    item = static_cast<uint32>(items.size());
                    items.push_back(
                        { charType, &object, destination.offset, 0, count, ValueItem });
                    destinationItems[destination] = item;
                    ScanType(charType);
                }
                else if (subType == TagfileArray && offset + 12 <= object.data.size()) {
                    const std::string_view objectType = tagfileTypes[object.type].name;
                    const uint32 countOffset =
                        objectType.starts_with("hkp") ? 4 : 8;
                    const uint32 count = ReadUint32(object, offset + countOffset);
                    if (count) {
                        const uint16 element = tagfileTypes[base].pointer;
                        item = static_cast<uint32>(items.size());
                        items.push_back({ element, &object, destination.offset, 0, count,
                                         ValueItem });
                        destinationItems[destination] = item;
                        ScanType(element);
                    }
                }
            }

            referenceItems[address] = item;
        }

        void SerializeClass(uint16 type, const ReflectedObject& object,
            uint32 nativeOffset, uint32 destinationOffset) {
            const TagfileType& value = tagfileTypes[type];
            if (value.parent) {
                SerializeClass(value.parent, object, nativeOffset, destinationOffset);
            }
            for (uint16 i = 0; i < value.numMembers; i++) {
                const uint16 memberIndex = value.memberBegin + i;
                const TagfileMember& member = tagfileMembers[memberIndex];
                if (!(member.flags & 1) &&
                    !(std::string_view(tagfileTypes[object.type].name).starts_with(
                        "hkp") &&
                      std::string_view(value.name) == "hkReferencedObject" &&
                      std::string_view(member.name) == "propertyBag")) {
                    const uint32 outputOffset =
                        destinationOffset + memberOffsets[memberIndex];
                    if (!WriteMember(type, member, object, outputOffset)) {
                        SerializeValue(member.type, object,
                            nativeOffset + NativeMemberOffset(type, member),
                            outputOffset);
                    }
                }
            }
        }

        uint32 NativeMemberOffset(uint16 type, const TagfileMember& member) const {
            const std::string_view className = tagfileTypes[type].name;
            const std::string_view memberName = member.name;
            if (className == "hkpBvCompressedMeshShape") {
                if (memberName == "convexRadius")
                    return 24;
                if (memberName == "weldingType")
                    return 28;
                if (memberName == "hasPerPrimitiveCollisionFilterInfo")
                    return 29;
                if (memberName == "hasPerPrimitiveUserData")
                    return 30;
                if (memberName == "collisionFilterInfoPalette")
                    return 32;
                if (memberName == "userDataPalette")
                    return 44;
                if (memberName == "userStringPalette")
                    return 56;
                if (memberName == "tree")
                    return 80;
            }
            else if (className == "hkcdStaticMeshTree") {
                if (memberName == "packedVertices")
                    return 96;
                if (memberName == "sharedVertices")
                    return 108;
                if (memberName == "primitiveDataRuns")
                    return 120;
            }
            else if (className == "hkcdStaticMeshTreeBase") {
                if (memberName == "sections")
                    return 60;
                if (memberName == "primitives")
                    return 72;
                if (memberName == "sharedVerticesIndex")
                    return 84;
            }
            else if (className == "hkpStaticCompoundShape") {
                if (memberName == "numBitsForChildShapeKey")
                    return 24;
                if (memberName == "referencePolicy")
                    return 25;
                if (memberName == "childShapeKeyMask")
                    return 28;
                if (memberName == "instances")
                    return 32;
                if (memberName == "instanceExtraInfos")
                    return 44;
                if (memberName == "disabledLargeShapeKeyTable")
                    return 48;
                if (memberName == "tree")
                    return 64;
            }
            else if (className == "hkpStaticCompoundShape::Instance") {
                if (memberName == "filterInfo")
                    return 52;
                if (memberName == "childFilterInfoMask")
                    return 56;
                if (memberName == "userData")
                    return 60;
            }
            else if (className == "hkpCapsuleShape") {
                if (memberName == "vertexA")
                    return 32;
                if (memberName == "vertexB")
                    return 48;
            }
            else if (className == "hkpConvexShape" && memberName == "radius") {
                return 16;
            }
            else if (className == "hkpBvTreeShape" && memberName == "bvTreeType") {
                return 16;
            }
            return member.offset;
        }

        uint32 ShapeUserData(const ReflectedObject& object) const {
            if (std::string_view(tagfileTypes[object.type].name) !=
                "hkpBvCompressedMeshShape") {
                return 0;
            }
            for (const ReflectedObject& owner : objects) {
                if (std::string_view(tagfileTypes[owner.type].name) !=
                    "hkpStaticCompoundShape") {
                    continue;
                }
                for (const hkFixup& fixup : owner.fixups) {
                    if (fixup.destClass == object.instance &&
                        fixup.strOffset + 16 <= owner.data.size()) {
                        return ReadUint32(owner, static_cast<uint32>(fixup.strOffset + 12));
                    }
                }
            }
            return 0;
        }

        bool WriteMember(uint16 type, const TagfileMember& member,
            const ReflectedObject& object, uint32 outputOffset) {
            const std::string_view className = tagfileTypes[type].name;
            const std::string_view memberName = member.name;
            if (className == "hkcdShape") {
                WriteUint32(data, outputOffset, memberName == "dispatchType" ? 4 : 0);
                return true;
            }
            if (className == "hkpShape" && memberName == "userData") {
                WriteUint32(data, outputOffset, ShapeUserData(object));
                return true;
            }
            return false;
        }

        void SerializeValue(uint16 type, const ReflectedObject& object,
            uint32 nativeOffset, uint32 destinationOffset) {
            const uint16 base = SuperType(type);
            if (!base || nativeOffset >= object.data.size()) {
                return;
            }

            const uint8 subType = SubType(base);
            if (subType == TagfileString || subType == TagfilePointer ||
                subType == TagfileArray) {
                const auto item = referenceItems.find({ &object, nativeOffset });
                const uint32 itemIndex = item == referenceItems.end() ? 0 : item->second;
                WriteUint32(data, destinationOffset, itemIndex);
                if (itemIndex) {
                    patches[base].push_back(destinationOffset);
                }
            }
            else if (subType == TagfileClass) {
                SerializeClass(type, object, nativeOffset, destinationOffset);
            }
            else if (subType == TagfileTuple) {
                const TagfileType& tuple = tagfileTypes[base];
                const uint32 sourceSize = NativeSize(tuple.pointer, object);
                const uint32 destinationSize = layouts[tuple.pointer].size;
                const uint32 count = tuple.subTypeFlags >> 8;
                for (uint32 i = 0; i < count; i++) {
                    SerializeValue(tuple.pointer, object, nativeOffset + i * sourceSize,
                        destinationOffset + i * destinationSize);
                }
            }
            else {
                const uint32 size =
                    std::min(layouts[type].size,
                        static_cast<uint32>(object.data.size() - nativeOffset));
                if (destinationOffset + size <= data.size()) {
                    std::copy_n(object.data.data() + nativeOffset, size,
                        data.data() + destinationOffset);
                }
            }
        }

        void BuildDataItem(uint32 index) {
            TagItem& item = items[index];
            const PortableLayout& layout = layouts[item.type];
            Pad(data, std::max<uint32>(layout.alignment, 2));
            item.offset = static_cast<uint32>(data.size());
            data.resize(data.size() + layout.size * item.count);
            for (uint32 n = 0; n < item.count; n++) {
                SerializeValue(item.type, *item.object,
                    item.nativeOffset +
                    n * NativeSize(item.type, *item.object),
                    item.offset + n * layout.size);
            }
        }

        void BuildData() {
            for (uint32 i = 1; i < items.size(); i++) {
                BuildDataItem(i);
            }
            Pad(data, 16);
        }

        uint32 TypeString(std::vector<std::string_view>& strings,
            std::string_view value) {
            const auto found = std::find(strings.begin(), strings.end(), value);
            if (found != strings.end()) {
                return static_cast<uint32>(found - strings.begin());
            }
            strings.push_back(value);
            return static_cast<uint32>(strings.size() - 1);
        }

        void WriteTypes() {
            const Chunk typeChunk = BeginChunk(wr, "TYPE", BranchChunk);

            const Chunk pointers = BeginChunk(wr, "TPTR", LeafChunk);
            wr.Skip(types.size() * 8);
            EndChunk(wr, pointers);

            std::vector<std::string_view> typeStrings;
            std::vector<std::string_view> fieldStrings;
            for (size_t i = 1; i < types.size(); i++) {
                const TagfileType& type = tagfileTypes[types[i]];
                TypeString(typeStrings, type.name);
                for (uint16 t = 0; t < type.numTemplates; t++) {
                    TypeString(typeStrings, tagfileTemplates[type.templateBegin + t].name);
                }
                for (uint16 m = 0; m < type.numMembers; m++) {
                    TypeString(fieldStrings, tagfileMembers[type.memberBegin + m].name);
                }
            }

            const Chunk typeStringChunk = BeginChunk(wr, "TSTR", LeafChunk);
            for (std::string_view value : typeStrings) {
                wr.WriteBuffer(value.data(), value.size());
                wr.Write<uint8>(0);
            }
            EndChunk(wr, typeStringChunk);

            const bool version2 = toolset >= HK2016_2;
            const Chunk typeNames =
                BeginChunk(wr, version2 ? "TNA1" : "TNAM", LeafChunk);
            WritePacked(wr, static_cast<uint32>(types.size()));
            for (size_t i = 1; i < types.size(); i++) {
                const TagfileType& type = tagfileTypes[types[i]];
                WritePacked(wr, TypeString(typeStrings, type.name));
                WritePacked(wr, type.numTemplates);
                for (uint16 t = 0; t < type.numTemplates; t++) {
                    const TagfileTemplate& item = tagfileTemplates[type.templateBegin + t];
                    WritePacked(wr, TypeString(typeStrings, item.name));
                    WritePacked(wr, item.name[0] == 't' ? metadataToType[item.value]
                        : item.value);
                }
            }
            if (version2) {
                while ((wr.Tell() - typeNames.offset) & 7) {
                    wr.Skip(1);
                }
            }
            EndChunk(wr, typeNames);

            const Chunk fieldStringChunk = BeginChunk(wr, "FSTR", LeafChunk);
            for (std::string_view value : fieldStrings) {
                wr.WriteBuffer(value.data(), value.size());
                wr.Write<uint8>(0);
            }
            EndChunk(wr, fieldStringChunk);

            const Chunk typeBodies =
                BeginChunk(wr, version2 ? "TBDY" : "TBOD", LeafChunk);
            for (size_t i = 1; i < types.size(); i++) {
                const uint16 typeIndex = types[i];
                const TagfileType& type = tagfileTypes[typeIndex];
                WritePacked(wr, static_cast<uint32>(i));
                WritePacked(wr, metadataToType[type.parent]);
                WritePacked(wr, type.flags);
                if (type.flags & 1) {
                    WritePacked(wr, type.subTypeFlags);
                }
                if (type.flags & 2) {
                    WritePacked(wr, metadataToType[type.pointer]);
                }
                if (type.flags & 4) {
                    WritePacked(wr, type.version);
                }
                if (type.flags & 8) {
                    WritePacked(wr, layouts[typeIndex].size);
                    WritePacked(wr, layouts[typeIndex].alignment);
                }
                if (type.flags & 0x10) {
                    WritePacked(wr, type.abstractValue);
                }
                if (type.flags & 0x20) {
                    uint32 members = 0;
                    uint32 properties = 0;
                    for (uint16 m = 0; m < type.numMembers; m++) {
                        if (tagfileMembers[type.memberBegin + m].flags & 1) {
                            properties++;
                        }
                        else {
                            members++;
                        }
                    }
                    WritePacked(wr, version2 ? members | (properties << 16)
                        : type.numMembers);
                    for (uint16 m = 0; m < type.numMembers; m++) {
                        const uint16 memberIndex = type.memberBegin + m;
                        const TagfileMember& member = tagfileMembers[memberIndex];
                        if (version2 && (member.flags & 1)) {
                            continue;
                        }
                        WritePacked(wr, TypeString(fieldStrings, member.name));
                        WritePacked(wr, member.flags);
                        WritePacked(wr, memberOffsets[memberIndex]);
                        WritePacked(wr, metadataToType[member.type]);
                    }
                }
                if (type.flags & 0x40) {
                    WritePacked(wr, type.numInterfaces);
                    for (uint16 n = 0; n < type.numInterfaces; n++) {
                        const TagfileInterface& item =
                            tagfileInterfaces[type.interfaceBegin + n];
                        WritePacked(wr, metadataToType[item.type]);
                        WritePacked(wr, item.flags);
                    }
                }
            }
            EndChunk(wr, typeBodies);

            const Chunk hashes = BeginChunk(wr, "THSH", LeafChunk);
            uint32 numHashes = 0;
            for (size_t i = 1; i < types.size(); i++) {
                numHashes += tagfileTypes[types[i]].hash != 0;
            }
            WritePacked(wr, numHashes);
            for (size_t i = 1; i < types.size(); i++) {
                const TagfileType& type = tagfileTypes[types[i]];
                if (type.hash) {
                    WritePacked(wr, static_cast<uint32>(i));
                    WriteLittleEndian(wr, type.hash);
                }
            }
            EndChunk(wr, hashes);

            const Chunk padding = BeginChunk(wr, "TPAD", LeafChunk);
            EndChunk(wr, padding);
            EndChunk(wr, typeChunk);
        }

        void WriteIndex() {
            const Chunk index = BeginChunk(wr, "INDX", BranchChunk);
            const Chunk itemChunk = BeginChunk(wr, "ITEM", LeafChunk);
            wr.Skip(12);
            for (size_t i = 1; i < items.size(); i++) {
                const TagItem& item = items[i];
                WriteLittleEndian(wr, metadataToType[item.type] | item.kind);
                WriteLittleEndian(wr, item.offset);
                WriteLittleEndian(wr, item.count);
            }
            EndChunk(wr, itemChunk);

            const Chunk patchChunk = BeginChunk(wr, "PTCH", LeafChunk);
            for (size_t i = 1; i < types.size(); i++) {
                auto found = patches.find(types[i]);
                if (found == patches.end()) {
                    continue;
                }
                auto& offsets = found->second;
                std::sort(offsets.begin(), offsets.end());
                offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
                WriteLittleEndian(wr, static_cast<uint32>(i));
                WriteLittleEndian(wr, static_cast<uint32>(offsets.size()));
                for (uint32 offset : offsets) {
                    WriteLittleEndian(wr, offset);
                }
            }
            EndChunk(wr, patchChunk);
            EndChunk(wr, index);
        }

        BinWritterRef_e wr;
        const IhkPackFile::VirtualClasses& classes;
        hkToolset toolset;
        const char* sdkVersion;
        std::vector<ReflectedObject> objects;
        std::vector<PortableLayout> layouts;
        std::vector<uint32> memberOffsets;
        std::unordered_map<ObjectAddress, const hkFixup*, ObjectAddressHash>
            fixupByAddress;
        std::vector<TagItem> items;
        std::unordered_map<const IhkVirtualClass*, uint32> objectItems;
        std::unordered_map<ObjectAddress, uint32, ObjectAddressHash> destinationItems;
        std::unordered_map<ObjectAddress, uint32, ObjectAddressHash> referenceItems;
        std::string data;
        std::vector<uint16> types;
        std::vector<uint16> metadataToType;
        std::vector<uint8> scannedTypes;
        std::unordered_map<uint16, std::vector<uint32>> patches;
    };

} // namespace

void IhkPackFile::ToTagFile(const std::string& fileName, hkToolset toolset) {
    const char* sdkVersion = SdkVersion(toolset);
    if (!sdkVersion) {
        return;
    }

    BinWritter writer(fileName);
    TagWriter(writer, GetAllClasses(), toolset, sdkVersion).Write();
}

std::vector<uint8> IhkPackFile::ToTagFile(hkToolset toolset) {
    const char* sdkVersion = SdkVersion(toolset);
    if (!sdkVersion) {
        return {};
    }

    MemoryStreamBuffer buffer;
    std::ostream stream(&buffer);
    BinWritterRef_e writer(stream);
    TagWriter(writer, GetAllClasses(), toolset, sdkVersion).Write();
    const std::string data = buffer.TakeData();
    return { data.begin(), data.end() };
}
