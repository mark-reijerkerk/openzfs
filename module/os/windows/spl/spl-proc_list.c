/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/procfs_list.h>
#include <sys/kstat.h>

typedef struct procfs_list_iter {
	procfs_list_t *pli_pl;
	void *pli_elt;
} pli_t;

void
seq_printf(struct seq_file *f, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	(void) vsnprintf(f->sf_buf, f->sf_size, fmt, adx);
	va_end(adx);
}

static int
procfs_list_update(kstat_t *ksp, int rw)
{
	procfs_list_t *pl = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		pl->pl_clear(pl);

	return (0);
}

static int
procfs_list_data(char *buf, size_t size, void *data)
{
	pli_t *p;
	void *elt;
	procfs_list_t *pl;
	struct seq_file f;

	p = data;
	pl = p->pli_pl;
	elt = p->pli_elt;
	ExFreePoolWithTag(p, '!SFZ');
	f.sf_buf = buf;
	f.sf_size = size;
	return (pl->pl_show(&f, elt));
}

static void *
procfs_list_addr(kstat_t *ksp, loff_t n)
{
	procfs_list_t *pl = ksp->ks_private;
	void *elt = ksp->ks_private1;
	pli_t *p = NULL;


	if (n == 0)
		ksp->ks_private1 = list_head(&pl->pl_list);
	else if (elt)
		ksp->ks_private1 = list_next(&pl->pl_list, elt);

	if (ksp->ks_private1) {
		p = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof (*p), '!SFZ');
		p->pli_pl = pl;
		p->pli_elt = ksp->ks_private1;
	}

	return (p);
}


void
procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	kstat_t *procfs_kstat;
	char *fullmod = module;
	char combined[KSTAT_STRLEN];

	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_show = show;
	procfs_list->pl_show_header = show_header;
	procfs_list->pl_clear = clear;
	procfs_list->pl_next_id = 1;
	procfs_list->pl_node_offset = procfs_list_node_off;

	// Unfortunately, "submodule" (ks_class) is not used when
	// considering unique names. Best way around that is to
	// make "module" be "module/submodule". Ie "zfs/poolname".
	if (submodule != NULL) {
		snprintf(combined, sizeof (combined), "%s/%s", module,
		    submodule);
		fullmod = combined;
	}

	procfs_kstat =  kstat_create(fullmod, 0, name, submodule,
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	if (procfs_kstat) {
		procfs_kstat->ks_lock = &procfs_list->pl_lock;
		procfs_kstat->ks_ndata = UINT32_MAX;
		procfs_kstat->ks_private = procfs_list;
		procfs_kstat->ks_update = procfs_list_update;
		kstat_set_seq_raw_ops(procfs_kstat, show_header,
		    procfs_list_data, procfs_list_addr);
		kstat_install(procfs_kstat);
	}

	procfs_list->pl_private = procfs_kstat;
}

void
procfs_list_uninstall(procfs_list_t *procfs_list)
{
}

void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	ASSERT(list_is_empty(&procfs_list->pl_list));
	if (procfs_list->pl_private != NULL)
		kstat_delete(procfs_list->pl_private);
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}

#define	NODE_ID(procfs_list, obj)			\
	(((procfs_list_node_t *)(((char *)obj) +	\
	(procfs_list)->pl_node_offset))->pln_id)

void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}
