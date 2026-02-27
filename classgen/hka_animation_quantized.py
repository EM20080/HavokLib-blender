from hka_animation import *

hkaQuantizedAnimation = ClassData('hkaQuantizedAnimation')
hkaQuantizedAnimation.inherits = [
    hkaAnimation,
]
hkaQuantizedAnimation.members = [
    hkArray('data', TYPES.char),
    ClassMember('endian', TYPES.uint32),
    ClassMember('skeleton', Pointer(TYPES.char)),
]

if __name__ == "__main__":
    CLASSES = [hkaQuantizedAnimation,]
    print("// This file has been automatically generated. Do not modify.")
    print('#include "hka_animation.inl"')

    dump_classes(CLASSES)
