set(SOURCES
	main.cpp
)

add_executable(startnetwork \${SOURCES})
banan_link_library(startnetwork ban)
banan_link_library(startnetwork libc)

install(TARGETS startnetwork)
