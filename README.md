## Explain about sfotware
	This software is for thermal-aware user level scheduler.
	Run the SPECCPU 2006 benchmark with determined CPU numbers. Check CPU temperature and migrate benchmark thread on highest temperature core to lowest temperature core.

## How to build
	Compile specshell with bench_launcher.

## Instruction
	1. Put SPEC CPU benchmark binary files under specset folder.
	2. Run the compiled excutable file.
	3. Then, type the command to specshell.
		run <benchmark> <cpu#> -i <input data>
	
