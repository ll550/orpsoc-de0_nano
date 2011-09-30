# Modelsim script generation, compile and run rules for board simulations

#
# Modelsim-specific settings
#
VOPT_ARGS+=$(QUIET) -suppress 2241

# If VCD dump is desired, tell Modelsim not to optimise
# away everything.
ifeq ($(VCD), 1)
#VOPT_ARGS=-voptargs="+acc=rnp"
VOPT_ARGS+=+acc=rnpqv
endif

# VSIM commands
# Suppressed warnings - 3009: Failed to open $readmemh() file
# Suppressed warnings - 3009: Module 'blah' does not have a `timescale directive in effect, but previous modules do.
# Suppressed warnings - 8598: Non-positive replication multiplier inside concat. Replication will be ignored
VSIM_ARGS+=  -suppress 7 -suppress 3009 -suppress 8598 -c $(QUIET) -do "set StdArithNoWarnings 1; run -all; exit"

# VPI debugging interface set up
VPI_SRC_C_DIR=$(COMMON_BENCH_VERILOG_DIR)/vpi/c
VPI_SRCS=$(shell ls $(VPI_SRC_C_DIR)/*.[ch])

# Modelsim VPI compile variables
MODELTECH_VPILIB=msim_jp_vpi.sl

# Modelsim VPI settings
ifeq ($(VPI), 1)
VPI_LIBS=$(VPI_SRC_C_DIR)/$(MODELTECH_VPILIB)
VSIM_ARGS += -pli $(VPI_SRC_C_DIR)/$(MODELTECH_VPILIB)
endif

# Rule to make the VPI library for modelsim
$(VPI_SRC_C_DIR)/$(MODELTECH_VPILIB): $(VPI_SRCS)
	$(MAKE) -C $(VPI_SRC_C_DIR) $(MODELTECH_VPILIB)

#
# Script generation rules
#

# Backend script generation - make these rules sensitive to source and includes
modelsim_backend.scr: $(BOARD_BACKEND_VERILOG_SRC)
	$(Q)echo "+incdir+"$(TECHNOLOGY_BACKEND_VERILOG_DIR) > $@;
	$(Q)echo "-y " $(BOARD_BACKEND_VERILOG_DIR) >> $@;
	$(Q)for vsrc in $(BACKEND_TECHNOLOGY_VERILOG_SRC); do echo $$vsrc >> $@; done
	$(Q)echo "-y " $(TECHNOLOGY_LIBRARY_VERILOG_DIR) >> $@;
	$(Q)echo "+libext+.v" >> $@;
	$(Q)echo >> $@;

# DUT compile script
modelsim_dut.scr: $(RTL_VERILOG_SRC) $(RTL_VERILOG_INCLUDES) $(BOOTROM_VERILOG)
	$(Q)echo "+incdir+"$(BOARD_RTL_VERILOG_INCLUDE_DIR) > $@;
	$(Q)echo "+incdir+"$(BOOTROM_SW_DIR) >> $@;
	$(Q)echo "+incdir+"$(BOARD_BENCH_VERILOG_INCLUDE_DIR) >> $@;
	$(Q)echo "+libext+.v" >> $@;
	$(Q)for module in $(BOARD_RTL_VERILOG_MODULES); do if [ -d $(BOARD_RTL_VERILOG_DIR)/$$module ]; then echo "-y " $(BOARD_RTL_VERILOG_DIR)/$$module >> $@; fi; done
	$(Q)for module in $(COMMON_RTL_VERILOG_MODULES); do if [ -d $(COMMON_RTL_VERILOG_DIR)/$$module ]; then echo "-y " $(COMMON_RTL_VERILOG_DIR)/$$module >> $@; fi; done
	$(Q)echo "-y " $(BOARD_BACKEND_VERILOG_DIR) >> $@;
	$(Q)if [ ! -z "$$GATELEVEL_MODULES" ]; \
		then echo "-y " $(BOARD_SYN_OUT_DIR) >> $@; \
		echo "+libext+.vm" >> $@; \
	fi
ifeq ($(FPGA_VENDOR), xilinx)
	$(Q)echo "-y "$(TECHNOLOGY_LIBRARY_VERILOG_DIR)"/src/unisims" >> $@;
	$(Q)echo "-y "$(TECHNOLOGY_LIBRARY_VERILOG_DIR)"/src/XilinxCoreLib" >> $@;
endif
	$(Q)echo >> $@

modelsim_bench.scr: $(BOARD_BENCH_VERILOG_SRC) $(COMMON_BENCH_VERILOG_SRC)
	$(Q)echo "+incdir+"$(BOARD_BENCH_VERILOG_INCLUDE_DIR) > $@;
	$(Q)echo "+incdir+"$(COMMON_BENCH_VERILOG_INCLUDE_DIR) >> $@;
	$(Q)for path in $(BENCH_VERILOG_SUBDIRS); do echo "+incdir+"$$path >> $@; done
	$(Q)for path in $(BENCH_VERILOG_SUBDIRS); do echo "-y "$$path >> $@; done
	$(Q)echo "+incdir+"$(BOARD_RTL_VERILOG_INCLUDE_DIR) >> $@;
	$(Q)echo "+libext+.v" >> $@;
	$(Q)for vsrc in $(BOARD_BENCH_VERILOG_SRC); do echo $$vsrc >> $@; done
	$(Q)for vsrc in $(COMMON_BENCH_VERILOG_SRC); do echo $$vsrc >> $@; done
ifeq ($(FPGA_VENDOR), xilinx)
	$(Q)echo "+incdir+"$(TECHNOLOGY_LIBRARY_VERILOG_DIR)"/src" >> $@;
endif
	$(Q)echo >> $@

#
# Build rules
#

# Modelsim backend library compilation rules
BACKEND_LIB=lib_backend
$(BACKEND_LIB): modelsim_backend.scr
	$(Q)if [ ! -e $@ ]; then vlib $@; fi
	$(Q)echo; echo "\t### Compiling backend library ###"; echo
	$(Q)vlog -nologo $(QUIET) -work $@ -f $<

# Compile DUT into "work" library
work: modelsim_dut.scr
	$(Q)if [ ! -e $@ ]; then vlib $@; fi
	$(Q)echo; echo "\t### Compiling Verilog design library ###"; echo
	$(Q)vlog $(QUIET) -f $< $(DUT_TOP_FILE)
	$(Q)if [ "$(RTL_VHDL_SRC)" != "" ]; then \
		echo; echo "\t### Compiling VHDL design library ###"; \
		echo; \
		vcom -93 $(QUIET) $(RTL_VHDL_SRC); \
	fi

#
# Run rule, one for each vendor
#

.PHONY : $(MODELSIM)
ifeq ($(FPGA_VENDOR), actel)
$(MODELSIM): modelsim_bench.scr $(TEST_DEFINES_VLG) $(BACKEND_LIB) $(VPI_LIBS) work
	$(Q)echo; echo "\t### Compiling testbench ###"; echo
	$(Q)vlog $(QUIET) -nologo -incr $(BENCH_TOP_FILE) -f $<
	$(Q)vopt $(QUIET) $(RTL_TESTBENCH_TOP_NAME) $(VOPT_ARGS) \
	-L $(BACKEND_LIB) -o tb
	$(Q)echo; echo "\t### Launching simulation ###"; echo
	$(Q)vsim $(VSIM_ARGS) tb
endif

ifeq ($(FPGA_VENDOR), xilinx)
$(MODELSIM): modelsim_bench.scr $(TEST_DEFINES_VLG) $(VPI_LIBS) work
	$(Q)echo; echo "\t### Compiling Xilinx support libs, user design & testbench ###"; echo
ifeq ($(DO_XILINX_COMPXLIB), 1)
	$(Q)$(TECHNOLOGY_BACKEND_BIN_DIR)/compxlib $(XLIB_ARGS) 
endif
	$(Q)vlog $(QUIET) -nologo -incr $(BENCH_TOP_FILE) -f $<
	$(Q)vopt $(QUIET) glbl $(RTL_TESTBENCH_TOP_NAME) $(VOPT_ARGS) -o tb
	$(Q)echo; echo "\t### Launching simulation ###"; echo
	$(Q)vsim $(VSIM_ARGS) tb
endif

ifeq ($(FPGA_VENDOR), altera)
$(MODELSIM): modelsim_bench.scr $(TEST_DEFINES_VLG) $(BACKEND_LIB) $(VPI_LIBS) work
	$(Q)echo; echo "\t### Compiling testbench ###"; echo
	$(Q)vlog $(QUIET) -nologo -incr $(BENCH_TOP_FILE) -f $<
	$(Q)vopt $(QUIET) $(RTL_TESTBENCH_TOP_NAME) $(VOPT_ARGS) \
	-L $(BACKEND_LIB) -o tb
	$(Q)echo; echo "\t### Launching simulation ###"; echo
	$(Q)vsim $(VSIM_ARGS) tb
endif