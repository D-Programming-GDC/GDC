From 2bbf6c81e3f4c9048356bbe67d7a469b7463b7c4 Mon Sep 17 00:00:00 2001
From: Johannes Pfau <johannespfau@gmail.com>
Date: Sat, 23 Feb 2013 19:09:51 +0100
Subject: [PATCH] Implement D predefined OS versions

This implements the following official versions:
* Windows
** Win32
** Win64
** Cygwin
** MinGW
* linux
* OSX
* FreeBSD
* OpenBSD
* NetBSD
* Solaris
* Posix
* AIX
* SysV4
* Hurd
* Android

These gdc specific versions are also implemented:
* GNU_MinGW64 (for mingw-w64)
* GNU_OpenSolaris (for opensolaris)
* GNU_GLibc (implemented for linux & bsd & opensolaris)
* GNU_UCLibc (implemented for linux)
* GNU_Bionic (implemented for linux)

These official OS versions are not implemented:
* DragonFlyBSD
* BSD (other BSDs)
* Haiku
* SkyOS
* SysV3

gdc-4.7 does not support Android on MIPS or i386.
Use gdc-4.8 for that.
---
 arm/linux-eabi.h   |  9 +++++++++
 darwin.h           |  6 ++++++
 freebsd.h          |  7 +++++++
 gnu.h              |  8 ++++++++
 i386/cygwin.h      |  7 +++++++
 i386/mingw-w64.h   |  7 +++++++
 i386/mingw32.h     | 12 ++++++++++++
 i386/sysv4.h       |  7 +++++++
 ia64/sysv4.h       |  7 +++++++
 kfreebsd-gnu.h     |  8 ++++++++
 knetbsd-gnu.h      | 10 ++++++++++
 kopensolaris-gnu.h | 10 ++++++++++
 linux-android.h    |  6 ++++++
 linux.h            | 14 ++++++++++++++
 netbsd.h           |  8 ++++++++
 openbsd.h          |  8 ++++++++
 rs6000/aix.h       |  7 +++++++
 rs6000/sysv4.h     |  7 +++++++
 sparc/sysv4.h      |  7 +++++++
 19 files changed, 155 insertions(+)

diff --git a/config/arm/linux-eabi.h b/config/arm/linux-eabi.h
index 80bd825..5167c8c 100644
--- a/config/arm/linux-eabi.h
+++ b/config/arm/linux-eabi.h
@@ -31,6 +31,15 @@
     }						\
   while (false)
 
+#undef  TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS() 			\
+  do 						\
+    {						\
+      TARGET_GENERIC_LINUX_OS_D_BUILTINS();	\
+      ANDROID_TARGET_OS_D_BUILTINS();		\
+    }						\
+  while (false)
+
 /* We default to a soft-float ABI so that binaries can run on all
    target hardware.  */
 #undef  TARGET_DEFAULT_FLOAT_ABI
diff --git a/config/darwin.h b/config/darwin.h
index 3e6efd7..37db444 100644
--- a/config/darwin.h
+++ b/config/darwin.h
@@ -952,4 +952,10 @@ extern void darwin_driver_init (unsigned int *,struct cl_decoded_option **);
    providing an osx-version-min of this unless overridden by the User.  */
 #define DEF_MIN_OSX_VERSION "10.4"
 
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+	builtin_define ("OSX");					\
+	builtin_define ("Posix");				\
+    } while (0)
+
 #endif /* CONFIG_DARWIN_H */
diff --git a/config/freebsd.h b/config/freebsd.h
index f9a4713..c9bd426 100644
--- a/config/freebsd.h
+++ b/config/freebsd.h
@@ -33,6 +33,13 @@ along with GCC; see the file COPYING3.  If not see
 #undef  TARGET_OS_CPP_BUILTINS
 #define TARGET_OS_CPP_BUILTINS() FBSD_TARGET_OS_CPP_BUILTINS()
 
+#undef TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+	builtin_define ("FreeBSD");				\
+	builtin_define ("Posix");				\
+    } while (0)
+
 #undef  CPP_SPEC
 #define CPP_SPEC FBSD_CPP_SPEC
 
diff --git a/config/gnu.h b/config/gnu.h
index dddbcbf..fac63da 100644
--- a/config/gnu.h
+++ b/config/gnu.h
@@ -40,3 +40,11 @@ along with GCC.  If not, see <http://www.gnu.org/licenses/>.
 	builtin_assert ("system=unix");		\
 	builtin_assert ("system=posix");	\
     } while (0)
+
+#undef TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("Hurd");				\
+	builtin_define ("Posix");				\
+    } while (0)
diff --git a/config/i386/cygwin.h b/config/i386/cygwin.h
index 5cf7f9c..e22c396 100644
--- a/config/i386/cygwin.h
+++ b/config/i386/cygwin.h
@@ -21,6 +21,13 @@ along with GCC; see the file COPYING3.  If not see
 
 #define EXTRA_OS_CPP_BUILTINS()  /* Nothing.  */
 
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+      builtin_define ("Windows");				\
+      builtin_define ("Cygwin");				\
+      builtin_define ("Posix");					\
+    } while (0)
+
 #undef CPP_SPEC
 #define CPP_SPEC "%(cpp_cpu) %{posix:-D_POSIX_SOURCE} \
   -D__CYGWIN32__ -D__CYGWIN__ %{!ansi:-Dunix} -D__unix__ -D__unix \
diff --git a/config/i386/mingw-w64.h b/config/i386/mingw-w64.h
index a45ce28..91145c7 100644
--- a/config/i386/mingw-w64.h
+++ b/config/i386/mingw-w64.h
@@ -85,3 +85,10 @@ along with GCC; see the file COPYING3.  If not see
   %{static:-Bstatic} %{!static:-Bdynamic} \
   %{shared|mdll: " SUB_LINK_ENTRY " --enable-auto-image-base} \
   %(shared_libgcc_undefs)"
+
+#undef TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+      TARGET_GENERIC_MINGW_OS_D_BUILTINS();			\
+      builtin_define ("GNU_MinGW64");				\
+    } while (0)
diff --git a/config/i386/mingw32.h b/config/i386/mingw32.h
index 0e3751f..747ff52 100644
--- a/config/i386/mingw32.h
+++ b/config/i386/mingw32.h
@@ -54,6 +54,18 @@ along with GCC; see the file COPYING3.  If not see
     }								\
   while (0)
 
+#define TARGET_OS_D_BUILTINS() TARGET_GENERIC_MINGW_OS_D_BUILTINS()
+#define TARGET_GENERIC_MINGW_OS_D_BUILTINS()			\
+    do {							\
+      builtin_define ("Windows");				\
+      builtin_define ("MinGW");					\
+								\
+      if (TARGET_64BIT && ix86_abi == MS_ABI)			\
+	  builtin_define ("Win64");				\
+      else if (!TARGET_64BIT)					\
+        builtin_define ("Win32");				\
+    } while (0)
+
 #ifndef TARGET_USE_PTHREAD_BY_DEFAULT
 #define SPEC_PTHREAD1 "pthread"
 #define SPEC_PTHREAD2 "!no-pthread"
diff --git a/config/i386/sysv4.h b/config/i386/sysv4.h
index 64026e7..eb4c904 100644
--- a/config/i386/sysv4.h
+++ b/config/i386/sysv4.h
@@ -71,3 +71,10 @@ along with GCC; see the file COPYING3.  If not see
 		   "|%0,_GLOBAL_OFFSET_TABLE_+(.-.LPR%=)}"		\
 	   : "=d"(BASE))
 #endif
+
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("SysV4");				\
+	builtin_define ("Posix");				\
+    } while (0)
diff --git a/config/ia64/sysv4.h b/config/ia64/sysv4.h
index 25fd224..430eec5 100644
--- a/config/ia64/sysv4.h
+++ b/config/ia64/sysv4.h
@@ -143,3 +143,10 @@ do {									\
 
 #define SDATA_SECTION_ASM_OP "\t.sdata"
 #define SBSS_SECTION_ASM_OP "\t.sbss"
+
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("SysV4");				\
+	builtin_define ("Posix");				\
+    } while (0)
diff --git a/config/kfreebsd-gnu.h b/config/kfreebsd-gnu.h
index 1a9747d..88b8dc2 100644
--- a/config/kfreebsd-gnu.h
+++ b/config/kfreebsd-gnu.h
@@ -30,6 +30,14 @@ along with GCC; see the file COPYING3.  If not see
     }						\
   while (0)
 
+#undef TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+	builtin_define ("FreeBSD");				\
+	builtin_define ("Posix");				\
+	builtin_define ("GNU_GLibc");				\
+    } while (0)
+
 #define GNU_USER_DYNAMIC_LINKER        GLIBC_DYNAMIC_LINKER
 #define GNU_USER_DYNAMIC_LINKER32      GLIBC_DYNAMIC_LINKER32
 #define GNU_USER_DYNAMIC_LINKER64      GLIBC_DYNAMIC_LINKER64
diff --git a/config/knetbsd-gnu.h b/config/knetbsd-gnu.h
index 30fa99a..ad2be41 100644
--- a/config/knetbsd-gnu.h
+++ b/config/knetbsd-gnu.h
@@ -31,6 +31,16 @@ along with GCC; see the file COPYING3.  If not see
     }						\
   while (0)
 
+#undef TARGET_OS_D_BUILTINS
+#define TARGET_OS_D_BUILTINS()			\
+  do						\
+    {						\
+      builtin_define ("NetBSD");		\
+      builtin_define ("Posix");			\
+      builtin_define ("GNU_GLibc");		\
+    }						\
+  while (0)
+
 
 #undef GNU_USER_DYNAMIC_LINKER
 #define GNU_USER_DYNAMIC_LINKER "/lib/ld.so.1"
diff --git a/config/kopensolaris-gnu.h b/config/kopensolaris-gnu.h
index ba6bdc5..174a280 100644
--- a/config/kopensolaris-gnu.h
+++ b/config/kopensolaris-gnu.h
@@ -31,5 +31,15 @@ along with GCC; see the file COPYING3.  If not see
     }						\
   while (0)
 
+#define TARGET_OS_D_BUILTINS()				\
+  do							\
+    {							\
+	builtin_define ("Solaris");			\
+	builtin_define ("Posix");			\
+	builtin_define ("GNU_OpenSolaris");		\
+	builtin_define ("GNU_GLibc");			\
+    }							\
+  while (0)
+
 #undef GNU_USER_DYNAMIC_LINKER
 #define GNU_USER_DYNAMIC_LINKER "/lib/ld.so.1"
diff --git a/config/linux-android.h b/config/linux-android.h
index 94c5274..e6ace8f 100644
--- a/config/linux-android.h
+++ b/config/linux-android.h
@@ -26,6 +26,12 @@
 	  builtin_define ("__ANDROID__");			\
     } while (0)
 
+#define ANDROID_TARGET_OS_D_BUILTINS()				\
+    do {							\
+	if (OPTION_ANDROID)					\
+	  builtin_define ("Android");				\
+    } while (0)
+
 #if ANDROID_DEFAULT
 # define NOANDROID "mno-android"
 #else
diff --git a/config/linux.h b/config/linux.h
index fb459e6..53597ee 100644
--- a/config/linux.h
+++ b/config/linux.h
@@ -50,6 +50,20 @@ see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
 	builtin_assert ("system=posix");			\
     } while (0)
 
+#define TARGET_OS_D_BUILTINS() TARGET_GENERIC_LINUX_OS_D_BUILTINS()
+#define TARGET_GENERIC_LINUX_OS_D_BUILTINS()			\
+    do {							\
+	if (OPTION_GLIBC)					\
+	  builtin_define ("GNU_GLibc");				\
+	else if (OPTION_UCLIBC)					\
+	  builtin_define ("GNU_UCLibc");			\
+	else if (OPTION_BIONIC)					\
+	  builtin_define ("GNU_Bionic");			\
+								\
+	builtin_define ("linux");				\
+	builtin_define ("Posix");				\
+    } while (0)
+
 /* Determine which dynamic linker to use depending on whether GLIBC or
    uClibc or Bionic is the default C library and whether
    -muclibc or -mglibc or -mbionic has been passed to change the default.  */
diff --git a/config/netbsd.h b/config/netbsd.h
index e9290c2..993f281 100644
--- a/config/netbsd.h
+++ b/config/netbsd.h
@@ -30,6 +30,14 @@ along with GCC; see the file COPYING3.  If not see
     }						\
   while (0)
 
+#define TARGET_OS_D_BUILTINS()			\
+  do						\
+    {						\
+      builtin_define ("NetBSD");		\
+      builtin_define ("Posix");			\
+    }						\
+  while (0)
+
 /* CPP_SPEC parts common to all NetBSD targets.  */
 #define NETBSD_CPP_SPEC				\
   "%{posix:-D_POSIX_SOURCE} \
diff --git a/config/openbsd.h b/config/openbsd.h
index fad2915..8a83b70 100644
--- a/config/openbsd.h
+++ b/config/openbsd.h
@@ -85,6 +85,14 @@ along with GCC; see the file COPYING3.  If not see
     }						\
   while (0)
 
+#define TARGET_OS_D_BUILTINS()			\
+  do						\
+    {						\
+      builtin_define ("OpenBSD");		\
+      builtin_define ("Posix");			\
+    }						\
+  while (0)
+
 /* TARGET_OS_CPP_BUILTINS() common to all OpenBSD ELF targets.  */
 #define OPENBSD_OS_CPP_BUILTINS_ELF()		\
   do						\
diff --git a/config/rs6000/aix.h b/config/rs6000/aix.h
index 29eabbb..ca9e313 100644
--- a/config/rs6000/aix.h
+++ b/config/rs6000/aix.h
@@ -111,6 +111,13 @@
     }						\
   while (0)
 
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("AIX");					\
+	builtin_define ("Posix");				\
+    } while (0)
+
 /* Define appropriate architecture macros for preprocessor depending on
    target switches.  */
 
diff --git a/config/rs6000/sysv4.h b/config/rs6000/sysv4.h
index aed2a29..76003bd 100644
--- a/config/rs6000/sysv4.h
+++ b/config/rs6000/sysv4.h
@@ -538,6 +538,13 @@ extern int fixuplabelno;
   while (0)
 #endif
 
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("SysV4");				\
+	builtin_define ("Posix");				\
+    } while (0)
+
 #undef	ASM_SPEC
 #define	ASM_SPEC "%(asm_cpu) \
 %{,assembler|,assembler-with-cpp: %{mregnames} %{mno-regnames}} \
diff --git a/config/sparc/sysv4.h b/config/sparc/sysv4.h
index 9584974..8ab9f74 100644
--- a/config/sparc/sysv4.h
+++ b/config/sparc/sysv4.h
@@ -119,3 +119,10 @@ do { ASM_OUTPUT_ALIGN ((FILE), Pmode == SImode ? 2 : 3);		\
 
 #undef MCOUNT_FUNCTION
 #define MCOUNT_FUNCTION "*_mcount"
+
+#define TARGET_OS_D_BUILTINS()					\
+    do {							\
+								\
+	builtin_define ("SysV4");				\
+	builtin_define ("Posix");				\
+    } while (0)
-- 
1.7.11.7

