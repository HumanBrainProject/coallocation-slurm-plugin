# Makefile for job_submit/storage_aware plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_storage_aware.la

# Job submit storage_aware plugin.
job_submit_storage_aware_la_SOURCES = job_submit_storage_aware.c
job_submit_storage_aware_la_LDFLAGS = $(PLUGIN_FLAGS)
