/* ============================================================================
 * Extension module to interface ION's memory management functions and Python. 
 * It is based on ION's ``sdrwatch`` and ``psmwatch`` utilities.
 *
 * Author: Marc Sanchez Net
 * Date:   8/12/2019
 * Copyright (c) 2019, California Institute of Technology ("Caltech").  
 * U.S. Government sponsorship acknowledged.
 * =========================================================================== */

#include <platform.h>
#include <sdr.h>
#include <ion.h>
#include <zco.h>
#include <sdrxn.h>
#include <psm.h>
#include <memmgr.h>
#include <Python.h>

#include "_utils.c"

/* ============================================================================
 * === _mem module definitions
 * ============================================================================ */

// Docstring for this module
static char module_docstring[] =
    "Extension module to interface ION's memory management functions and Python.";

// Docstring for the functions being wrapped
static char sdr_dump_docstring[] =
    "Dump the state of the SDR.";
static char psm_dump_docstring[] =
    "Dump the state of the PSM.";

// Declare the functions to wrap
static PyObject *pyion_sdr_dump(PyObject *self, PyObject *args);
static PyObject *pyion_psm_dump(PyObject *self, PyObject *args);

// Define member functions of this module
static PyMethodDef module_methods[] = {
    {"sdr_dump", pyion_sdr_dump, METH_VARARGS, sdr_dump_docstring},
    {"psm_dump", pyion_psm_dump, METH_VARARGS, psm_dump_docstring},
    {NULL, NULL, 0, NULL}
};

/* ============================================================================
 * === Define _mem as a Python module
 * ============================================================================ */

PyMODINIT_FUNC PyInit__mem(void) {
    // Define variables
    PyObject *module;

    // Define module configuration parameters
    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_mem",
        module_docstring,
        -1,
        module_methods,
        NULL,
        NULL,
        NULL,
        NULL};

    // Create the module
    module = PyModule_Create(&moduledef);

    return module;
}

/* ============================================================================
 * === SDR-related functions
 * ============================================================================ */

static PyObject *pyion_sdr_dump(PyObject *self, PyObject *args) {
    // Define variables
    int             i;
    Sdr		        sdr;
	SdrUsageSummary	sdrUsage;
    char            *sdrName;
    PyObject        *sp_blocks = PyDict_New();
    PyObject        *lp_blocks = PyDict_New();
    size_t          sp_blk_size = 0;
    size_t          lp_blk_size = WORD_SIZE;

    // Parse the input tuple. Raises error automatically if not possible
    if (!PyArg_ParseTuple(args, "s", &sdrName))
        return NULL;

    // Attach to SDR and start using it
    sdr_initialize(0, NULL, SM_NO_KEY, NULL);
    sdr = sdr_start_using(sdrName);

    // If could not attach, raise error
    if (sdr == NULL) {
        pyion_SetExc(PyExc_MemoryError, "Could not attach to SDR with name '%s'.", sdrName);
        return NULL;
    }

    // Get the state of the SDR
    if (!sdr_pybegin_xn(sdr)) return NULL;
    sdr_usage(sdr, &sdrUsage);
    if (!sdr_end_xn(sdr)) return NULL;

    // Get amount of data available in small pool [bytes]
    size_t sp_avail = sdrUsage.smallPoolFree;
    size_t sp_used  = sdrUsage.smallPoolAllocated;
    size_t sp_total = sdrUsage.smallPoolSize;

    // Get amount of data available in large pool [bytes]
    size_t lp_avail = sdrUsage.largePoolFree;
    size_t lp_used  = sdrUsage.largePoolAllocated;
    size_t lp_total = sdrUsage.largePoolSize;

    // Head data, all in [bytes]
    size_t hp_size  = sdrUsage.heapSize;
    size_t hp_avail = sdrUsage.unusedSize;
    size_t hp_used  = hp_size - (sp_avail+lp_avail+hp_avail);
    size_t hp_max_u = hp_size - hp_avail;

    // Get the amount of blocks available in the small pool
    for (i = 0; i < SMALL_SIZES; i++) {
        sp_blk_size  += WORD_SIZE;
        PyObject *key = Py_BuildValue("k", (unsigned long)sp_blk_size);
        PyObject *val = Py_BuildValue("k", (unsigned long)sdrUsage.smallPoolFreeBlockCount[i]);
        PyDict_SetItem(sp_blocks, key, val);
    }

    // Get the amount of blocks available in the large pool
    for (i = 0; i < LARGE_ORDERS; i++) {
        lp_blk_size  *= 2;
        PyObject *key = Py_BuildValue("k", (unsigned long)lp_blk_size);
        PyObject *val = Py_BuildValue("k", (unsigned long)sdrUsage.largePoolFreeBlockCount[i]);
        PyDict_SetItem(lp_blocks, key, val);
    }

    // Stop using the SDR
    sdr_stop_using(sdr);

    // Return summary statistics in a dictionary
    return Py_BuildValue("({s:k, s:k, s:k, s:k, s:k, s:k, s:k, s:k, s:k, s:k}, O, O)",
                         "small_pool_avail",  (unsigned long)sp_avail,
                         "small_pool_used",   (unsigned long)sp_used,
                         "small_pool_total",  (unsigned long)sp_total,
                         "large_pool_avail",  (unsigned long)lp_avail,
                         "large_pool_used",   (unsigned long)lp_used,
                         "large_pool_total",  (unsigned long)lp_total,
                         "heap_size",         (unsigned long)hp_size,
                         "heap_avail",        (unsigned long)hp_avail,
                         "heap_used",         (unsigned long)hp_used,
                         "max_heap_used",     (unsigned long)hp_max_u,
                         sp_blocks, lp_blocks );
}

/* ============================================================================
 * === PSM-related functions
 * ============================================================================ */

static PyObject *pyion_psm_dump(PyObject *self, PyObject *args) {
    // Define variables
    int             memKey, i;
    long long       memSize;
    char            *partitionName;
    char		    *memory = NULL;
    uaddr		    smId = 0;
	PsmView		    memView;
	PsmPartition    psm = &memView;
    int		        memmgrIdx;
    PsmUsageSummary	psmUsage;
    PyObject        *sp_blocks = PyDict_New();
    PyObject        *lp_blocks = PyDict_New();
    size_t          sp_blk_size = 0;
    size_t          lp_blk_size = WORD_SIZE;

    // Parse the input tuple. Raises error automatically if not possible
    if (!PyArg_ParseTuple(args, "iLs", &memKey, &memSize, &partitionName))
        return NULL;

    // Initialize IPC
    if (sm_ipc_init() < 0) {
        pyion_SetExc(PyExc_MemoryError, "IPC initialization failed.");
        return NULL;
	}

    // Open memory manager
    if (memmgr_open(memKey, memSize, &memory, &smId, partitionName, &psm,
				&memmgrIdx, NULL, NULL, NULL, NULL) < 0) {
        pyion_SetExc(PyExc_MemoryError, "Can't attach to PSM with key '%i'.", memKey);
        return NULL;
	}

    // Get the state of the PSM
    psm_usage(psm, &psmUsage);

    // Get amount of data available in small pool [bytes]
    size_t sp_avail = psmUsage.smallPoolFree;
    size_t sp_used  = psmUsage.smallPoolAllocated;
    size_t sp_total = psmUsage.smallPoolSize;

    // Get amount of data available in large pool [bytes]
    size_t lp_avail = psmUsage.largePoolFree;
    size_t lp_used  = psmUsage.largePoolAllocated;
    size_t lp_total = psmUsage.largePoolSize;
    
    // Head data, all in [bytes]
    size_t wm_size  = psmUsage.partitionSize;
    size_t wm_avail = psmUsage.unusedSize;

    // Get the amount of blocks available in the small pool
    for (i = 0; i < SMALL_SIZES; i++) {
        sp_blk_size  += WORD_SIZE;
        PyObject *key = Py_BuildValue("k", (unsigned long)sp_blk_size);
        PyObject *val = Py_BuildValue("k", (unsigned long)psmUsage.smallPoolFreeBlockCount[i]);
        PyDict_SetItem(sp_blocks, key, val);
    }

    // Get the amount of blocks available in the large pool
    for (i = 0; i < LARGE_ORDERS; i++) {
        lp_blk_size  *= 2;
        PyObject *key = Py_BuildValue("k", (unsigned long)lp_blk_size);
        PyObject *val = Py_BuildValue("k", (unsigned long)psmUsage.largePoolFreeBlockCount[i]);
        PyDict_SetItem(lp_blocks, key, val);
    }

    // Return summary statistics in a dictionary
    return Py_BuildValue("({s:k, s:k, s:k, s:k, s:k, s:k, s:k, s:k}, O, O)",
                         "small_pool_avail",  (unsigned long)sp_avail,
                         "small_pool_used",   (unsigned long)sp_used,
                         "small_pool_total",  (unsigned long)sp_total,
                         "large_pool_avail",  (unsigned long)lp_avail,
                         "large_pool_used",   (unsigned long)lp_used,
                         "large_pool_total",  (unsigned long)lp_total,
                         "wm_size",           (unsigned long)wm_size,
                         "wm_avail",          (unsigned long)wm_avail,
                         sp_blocks, lp_blocks );
}
