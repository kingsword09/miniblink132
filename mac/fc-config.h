#ifndef MINIBLINK_MAC_FC_CONFIG_H_
#define MINIBLINK_MAC_FC_CONFIG_H_

#include "../third_party/fontconfig/include/fc-config.h"

/* The bundled fontconfig config was generated with gettext enabled, but the
 * mac build does not provide libintl. Keep the rest of the generated feature
 * checks and use fontconfig's no-NLS fallback path. */
#undef ENABLE_NLS
#undef HAVE_DCGETTEXT
#undef HAVE_GETTEXT
#undef HAVE_RANDOM_R
#define HAVE_STRUCT_STATFS_F_FSTYPENAME 1
#undef HAVE_STRUCT_STAT_ST_MTIM
#undef HAVE_SYS_STATFS_H
#undef HAVE_SYS_VFS_H

#endif /* MINIBLINK_MAC_FC_CONFIG_H_ */
