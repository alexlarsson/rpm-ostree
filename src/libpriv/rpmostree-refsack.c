/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Colin Walters <walters@verbum.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include "rpmostree-refsack.h"
#include "rpmostree-rpm-util.h"

RpmOstreeRefSack *
_rpm_ostree_refsack_new (HySack sack, int temp_base_dfd, const char *temp_path)
{
  RpmOstreeRefSack *rsack = g_new0 (RpmOstreeRefSack, 1);
  rsack->sack = sack;
  rsack->refcount = 1;
  rsack->temp_base_dfd = temp_base_dfd;
  rsack->temp_path = g_strdup (temp_path);
  return rsack;
}

RpmOstreeRefSack *
_rpm_ostree_refsack_ref (RpmOstreeRefSack *rsack)
{
  g_atomic_int_inc (&rsack->refcount);
  return rsack;
}

void
_rpm_ostree_refsack_unref (RpmOstreeRefSack *rsack)
{
  if (!g_atomic_int_dec_and_test (&rsack->refcount))
    return;
  hy_sack_free (rsack->sack);
  
  /* The sack might point to a temporarily allocated rpmdb copy, if so,
   * prune it now.
   */
  if (rsack->temp_path)
    (void) glnx_shutil_rm_rf_at (rsack->temp_base_dfd, rsack->temp_path, NULL, NULL);

  g_free (rsack->temp_path);
  g_free (rsack);
}

RpmOstreeRefSack *
_rpm_ostree_get_refsack_for_commit (OstreeRepo                *repo,
                                    const char                *ref,
                                    GCancellable              *cancellable,
                                    GError                   **error)
{
  g_autofree char *commit = NULL;
  g_autofree char *tempdir = g_strdup ("/tmp/rpmostree-dbquery-XXXXXXXX");
  OstreeRepoCheckoutOptions checkout_options = { 0, };
  glnx_fd_close int tempdir_dfd = -1;

  if (!ostree_repo_resolve_rev (repo, ref, FALSE, &commit, error))
    goto out;

  if (mkdtemp (tempdir) == NULL)
    {
      glnx_set_error_from_errno (error);
      goto out;
    }

  if (!glnx_opendirat (AT_FDCWD, tempdir, FALSE, &tempdir_dfd, error))
    goto out;

  /* Create intermediate dirs */ 
  if (!glnx_shutil_mkdir_p_at (tempdir_dfd, "usr/share", 0777, cancellable, error))
    goto out;

  checkout_options.mode = OSTREE_REPO_CHECKOUT_MODE_USER;
  checkout_options.subpath = "usr/share/rpm";

  if (!ostree_repo_checkout_tree_at (repo, &checkout_options,
                                     tempdir_dfd, "usr/share/rpm",
                                     commit, 
                                     cancellable, error))
    goto out;

  {
    HySack hsack; 

    if (!rpmostree_get_sack_for_root (tempdir_dfd, ".",
                                      &hsack, cancellable, error))
      goto out;

    return _rpm_ostree_refsack_new (hsack, AT_FDCWD, tempdir);
  }

 out:
  return NULL;
}

