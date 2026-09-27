Risc V 32 bit cpu made by jondor.

The cpu will be on discrete logic like 7400 series chips probably 74lvc chips and be printed as a physical pcb.

THe name is super32 because its super and its also 32 bit.

The project was started on 9/10/26 and it started as a c++ simulation file.

The source code consists of the generic fetch execute decode loop for now.

When the c++ version is finished I will move onto verilog.

c++ version has been moved to c becuase it is more predictable and more suted for this project.

The end goal is to boot tinycore linux or a graphical distro on it and get display out from vga or another common display cord, the specs will probably be 128mb of psram, 16mb of sram as cache, flip flops for registers, and probably multiple pcbs to fit everything.

