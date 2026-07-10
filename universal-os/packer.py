import struct
import sys

# Fat File Constants
FAT_MAGIC = 0xCAFEBABE
CPU_X86_64 = 1
CPU_ARM64 = 2

def create_fat_binary(x86_bin_path, arm_bin_path, output_path):
    with open(x86_bin_path, 'rb') as f:
        x86_data = f.read()
    with open(arm_bin_path, 'rb') as f:
        arm_data = f.read()

    arch_count = 2
    
    # Calculate offsets dynamically based on structural metadata sizing
    # fat_header: magic(4B) + arch_count(4B) = 8 Bytes
    # Each descriptor entry: type(4B) + offset(4B) + size(4B) + load_addr(4B) = 16 Bytes
    # Total header size = 8 + (2 * 16) = 40 Bytes
    header_size = 8 + (arch_count * 16)
    
    x86_offset = header_size
    x86_size = len(x86_data)
    
    arm_offset = x86_offset + x86_size
    arm_size = len(arm_data)

    # Pack Main Fat Header: Magic, Count
    header = struct.pack('>II', FAT_MAGIC, arch_count)
    
    # Pack Architecture Descriptors (Using Big-Endian structure for cross-arch parsing safety)
    # Fields: CPU_TYPE, OFFSET, SIZE, LOAD_ADDRESS
    header += struct.pack('>IIII', CPU_X86_64, x86_offset, x86_size, 0x00100000)
    header += struct.pack('>IIII', CPU_ARM64, arm_offset, arm_size, 0x40000000)

    # Write out unified compilation payload
    with open(output_path, 'wb') as out:
        out.write(header)
        out.write(x86_data)
        out.write(arm_data)
        
    print(f"[PACKER SUCCESS] Linked {output_path} successfully.")

if __name__ == "__main__":
    create_fat_binary('kernel_x86.bin', 'kernel_arm.bin', 'os_core.fat')
