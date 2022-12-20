#include <BAN/StringView.h>
#include <BAN/Vector.h>
#include <kernel/APIC.h>
#include <kernel/GDT.h>
#include <kernel/IDT.h>
#include <kernel/IO.h>
#include <kernel/Keyboard.h>
#include <kernel/kmalloc.h>
#include <kernel/kprint.h>
#include <kernel/multiboot.h>
#include <kernel/panic.h>
#include <kernel/PIC.h>
#include <kernel/PIT.h>
#include <kernel/RTC.h>
#include <kernel/Serial.h>
#include <kernel/Shell.h>
#include <kernel/tty.h>
#include <kernel/VESA.h>

#define DISABLE_INTERRUPTS() asm volatile("cli")
#define ENABLE_INTERRUPTS() asm volatile("sti")


multiboot_info_t* s_multiboot_info;

using namespace BAN;

struct ParsedCommandLine
{
	bool force_pic = false;
};

ParsedCommandLine ParseCommandLine(const char* command_line)
{
	auto args = MUST(StringView(command_line).Split([](char c) { return c == ' ' || c == '\t'; }));

	ParsedCommandLine result;
	result.force_pic = args.Has("noapic");
	return result;
}

extern "C" void kernel_main(multiboot_info_t* mbi, uint32_t magic)
{
	DISABLE_INTERRUPTS();

	Serial::initialize();
	if (magic != 0x2BADB002)
	{
		dprintln("Invalid multiboot magic number");
		return;
	}

	s_multiboot_info = mbi;

	if (!VESA::PreInitialize())
	{
		dprintln("Could not initialize VESA");
		return;
	}
	TTY::initialize();

	kmalloc_initialize();

	VESA::Initialize();

	ParsedCommandLine cmdline;
	if (mbi->flags & 0x02)
		cmdline = ParseCommandLine((const char*)mbi->cmdline);

	APIC::Initialize(cmdline.force_pic);
	gdt_initialize();
	IDT::initialize();

	PIT::initialize();
	if (!Keyboard::initialize())
		return;

	ENABLE_INTERRUPTS();

	kprintln("Hello from the kernel!");

	auto& shell = Kernel::Shell::Get();

	shell.Run();

	for (;;)
	{
		asm("hlt");
	}
}