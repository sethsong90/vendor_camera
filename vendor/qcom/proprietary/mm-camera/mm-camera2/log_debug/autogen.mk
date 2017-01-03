# ============================================================================
# ----------------------------------------------------------------------------
#           This makefile uses a python script which reads an xml
#           file in order to generate and output a header file
# ----------------------------------------------------------------------------


MMCAMERA2_PATH := $(abspath $(call my-dir)/..)

SCRIPT_PATH = $(MMCAMERA2_PATH)/log_debug/process_camscope_packet_type_file.py
PROCESSSCOPETOOL := python $(SCRIPT_PATH)
CAMSCOPE_SCRIPT_OUTDIR = $(MMCAMERA2_PATH)/includes
CAMSCOPE_SCRIPT_OUTPUTS = $(CAMSCOPE_SCRIPT_OUTDIR)/camscope_packet_type.h
CAMSCOPE_SCRIPT_INPUTS  = $(MMCAMERA2_PATH)/log_debug/camscope_packet_type.xml

.INTERMEDIATE: camscope-autogen

$(CAMSCOPE_SCRIPT_OUTPUTS) : $(CAMSCOPE_SCRIPT_INPUTS) camscope-autogen

camscope-autogen:
	$(PROCESSSCOPETOOL) $(CAMSCOPE_SCRIPT_INPUTS) $(CAMSCOPE_SCRIPT_OUTDIR)

LOCAL_GENERATED_SOURCES += $(CAMSCOPE_SCRIPT_OUTPUTS)
