cmake_minimum_required(VERSION 3.26)

project(bootloader ASM)

set(BOOTLOADER_SOURCES
	a20_line.S
	boot.S
	command_line.S
	disk.S
	elf.S
	ext2.S
	framebuffer.S
	memory_map.S
	utils.S
)

add_executable(bootloader ${BOOTLOADER_SOURCES})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
target_link_options(bootloader PRIVATE LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
target_link_options(bootloader PRIVATE -nostdlib)
