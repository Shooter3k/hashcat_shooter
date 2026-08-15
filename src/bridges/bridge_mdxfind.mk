MDXFIND_HASHPIPE := src/bridges/mdxfind/hashpipe
MDXFIND_HASHPIPE_3P := $(MDXFIND_HASHPIPE)/third_party

MDXFIND_BRIDGE_SOURCES := \
  src/bridges/bridge_mdxfind.c \
  src/bridges/mdxfind/hx_vm.c \
  src/bridges/mdxfind/hx_func.c \
  src/bridges/mdxfind/codegen/hx_specs_data.c \
  $(MDXFIND_HASHPIPE)/hashpipe_engine.c \
  $(MDXFIND_HASHPIPE)/crypt-des.c \
  $(MDXFIND_HASHPIPE)/myprogress.c

MDXFIND_BRIDGE_FLAGS := \
  -DHX_STANDALONE \
  -Isrc/bridges \
  -Isrc/bridges/mdxfind \
  -Isrc/bridges/mdxfind/codegen \
  -I$(MDXFIND_HASHPIPE) \
  -I$(MDXFIND_HASHPIPE_3P) \
  -I$(MDXFIND_HASHPIPE_3P)/sphlib \
  -I$(MDXFIND_HASHPIPE_3P)/mhash/include \
  -I$(MDXFIND_HASHPIPE_3P)/mhash/include/mutils \
  -I$(MDXFIND_HASHPIPE_3P)/mhash/lib \
  -I$(MDXFIND_HASHPIPE_3P)/rhash \
  -I$(MDXFIND_HASHPIPE_3P)/md6 \
  -I$(MDXFIND_HASHPIPE_3P)/gost2012 \
  -I$(MDXFIND_HASHPIPE_3P)/crypt_blowfish

MDXFIND_SPH_NAMES := blake bmw cubehash echo fugue groestl hamsi haval jh \
  keccak luffa md2 md4 md5 panama radiogatun ripemd sha0 sha1 sha2 sha2big \
  shabal shavite simd skein tiger whirlpool
MDXFIND_MHASH_NAMES := $(basename $(notdir $(wildcard $(MDXFIND_HASHPIPE_3P)/mhash/lib/*.c)))
MDXFIND_RHASH_NAMES := $(basename $(notdir $(wildcard $(MDXFIND_HASHPIPE_3P)/rhash/*.c)))
MDXFIND_MD6_NAMES := md6_compress md6_mode
MDXFIND_GOST_NAMES := sbob_pi64 sbob_tab64 streebog
MDXFIND_BCRYPT_NAMES := crypt_blowfish crypt_gensalt wrapper
MDXFIND_ARGON2_NAMES := argon2 core encoding thread opt
MDXFIND_YESCRYPT_NAMES := yescrypt-opt yescrypt-common sha256 insecure_memzero

define MDXFIND_OBJECT_LISTS
MDXFIND_SPH_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/sph/,$(addsuffix .o,$(MDXFIND_SPH_NAMES)))
MDXFIND_MHASH_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/mhash/,$(addsuffix .o,$(MDXFIND_MHASH_NAMES)))
MDXFIND_RHASH_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/rhash/,$(addsuffix .o,$(MDXFIND_RHASH_NAMES)))
MDXFIND_MD6_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/md6/,$(addsuffix .o,$(MDXFIND_MD6_NAMES)))
MDXFIND_GOST_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/gost/,$(addsuffix .o,$(MDXFIND_GOST_NAMES)))
MDXFIND_BCRYPT_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/bcrypt/,$(addsuffix .o,$(MDXFIND_BCRYPT_NAMES)))
MDXFIND_ARGON2_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/argon2/,$(addsuffix .o,$(MDXFIND_ARGON2_NAMES) blake2b))
MDXFIND_YESCRYPT_OBJS_$(1) := $(addprefix obj/hashpipe.$(1)/yescrypt/,$(addsuffix .o,$(MDXFIND_YESCRYPT_NAMES)))
MDXFIND_ARCHIVES_$(1) := obj/hashpipe.$(1)/libsph.a obj/hashpipe.$(1)/libmhash.a \
  obj/hashpipe.$(1)/librhash.a obj/hashpipe.$(1)/libmd6.a \
  obj/hashpipe.$(1)/libgost2012.a obj/hashpipe.$(1)/libcrypt_blowfish.a \
  obj/hashpipe.$(1)/libargon2.a obj/hashpipe.$(1)/libyescrypt.a
endef

$(eval $(call MDXFIND_OBJECT_LISTS,NATIVE))
$(eval $(call MDXFIND_OBJECT_LISTS,LINUX))
$(eval $(call MDXFIND_OBJECT_LISTS,WIN))

define MDXFIND_COMPILE_RULES
obj/hashpipe.$(1)/sph/%.o: $(MDXFIND_HASHPIPE_3P)/sphlib/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -fno-strict-aliasing -I$(MDXFIND_HASHPIPE_3P)/sphlib -c $$< -o $$@
obj/hashpipe.$(1)/mhash/%.o: $(MDXFIND_HASHPIPE_3P)/mhash/lib/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -std=gnu89 -O2 -w -fPIC -I$(MDXFIND_HASHPIPE_3P)/mhash/include -I$(MDXFIND_HASHPIPE_3P)/mhash/include/mutils -I$(MDXFIND_HASHPIPE_3P)/mhash/lib -c $$< -o $$@
obj/hashpipe.$(1)/rhash/%.o: $(MDXFIND_HASHPIPE_3P)/rhash/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -DRHASH_XVERSION=0x01040600 -I$(MDXFIND_HASHPIPE_3P)/rhash -c $$< -o $$@
obj/hashpipe.$(1)/md6/%.o: $(MDXFIND_HASHPIPE_3P)/md6/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -fcommon -I$(MDXFIND_HASHPIPE_3P)/md6 -c $$< -o $$@
obj/hashpipe.$(1)/gost/%.o: $(MDXFIND_HASHPIPE_3P)/gost2012/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -I$(MDXFIND_HASHPIPE_3P)/gost2012 -c $$< -o $$@
obj/hashpipe.$(1)/bcrypt/%.o: $(MDXFIND_HASHPIPE_3P)/crypt_blowfish/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -I$(MDXFIND_HASHPIPE_3P)/crypt_blowfish -c $$< -o $$@
obj/hashpipe.$(1)/argon2/%.o: $(MDXFIND_HASHPIPE_3P)/argon2/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -I$(MDXFIND_HASHPIPE_3P)/argon2 -c $$< -o $$@
obj/hashpipe.$(1)/argon2/blake2b.o: $(MDXFIND_HASHPIPE_3P)/argon2/blake2/blake2b.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -I$(MDXFIND_HASHPIPE_3P)/argon2 -c $$< -o $$@
obj/hashpipe.$(1)/yescrypt/%.o: $(MDXFIND_HASHPIPE_3P)/yescrypt/%.c
	@mkdir -p $$(dir $$@)
	$(2) $(CCFLAGS) $(3) -O2 -w -fPIC -DSKIP_MEMZERO -I$(MDXFIND_HASHPIPE_3P)/yescrypt -c $$< -o $$@
obj/hashpipe.$(1)/libsph.a: $$(MDXFIND_SPH_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libmhash.a: $$(MDXFIND_MHASH_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/librhash.a: $$(MDXFIND_RHASH_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libmd6.a: $$(MDXFIND_MD6_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libgost2012.a: $$(MDXFIND_GOST_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libcrypt_blowfish.a: $$(MDXFIND_BCRYPT_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libargon2.a: $$(MDXFIND_ARGON2_OBJS_$(1))
	$(4) rcs $$@ $$^
obj/hashpipe.$(1)/libyescrypt.a: $$(MDXFIND_YESCRYPT_OBJS_$(1))
	$(4) rcs $$@ $$^
endef

$(eval $(call MDXFIND_COMPILE_RULES,NATIVE,$(CC),$(CFLAGS_NATIVE),$(AR)))
$(eval $(call MDXFIND_COMPILE_RULES,LINUX,$(CC_LINUX),$(CFLAGS_CROSS_LINUX),$(AR_LINUX)))
$(eval $(call MDXFIND_COMPILE_RULES,WIN,$(CC_WIN),$(CFLAGS_CROSS_WIN),$(AR_WIN)))

MDXFIND_BRIDGE_LIBS_LINUX  := -lcrypto
MDXFIND_BRIDGE_LIBS_WIN    := -Wl,-Bstatic -lcrypto -Wl,-Bdynamic -liconv -lws2_32 -lgdi32 -lcrypt32
MDXFIND_BRIDGE_LIBS_NATIVE := $(MDXFIND_BRIDGE_LIBS_LINUX)

ifneq (,$(filter $(UNAME),CYGWIN MSYS2))
MDXFIND_BRIDGE_LIBS_NATIVE := $(MDXFIND_BRIDGE_LIBS_WIN)
endif

BRIDGE_SRC_bridge_mdxfind           := $(MDXFIND_BRIDGE_SOURCES)
BRIDGE_SRC_bridge_mdxfind_NATIVE    := $(MDXFIND_ARCHIVES_NATIVE)
BRIDGE_SRC_bridge_mdxfind_LINUX     := $(MDXFIND_ARCHIVES_LINUX)
BRIDGE_SRC_bridge_mdxfind_WIN       := $(MDXFIND_ARCHIVES_WIN)

BRIDGE_CFLAGS_bridge_mdxfind        := $(MDXFIND_BRIDGE_FLAGS)
BRIDGE_CFLAGS_bridge_mdxfind_NATIVE := $(MDXFIND_BRIDGE_LIBS_NATIVE)
BRIDGE_CFLAGS_bridge_mdxfind_LINUX  := $(MDXFIND_BRIDGE_LIBS_LINUX)
BRIDGE_CFLAGS_bridge_mdxfind_WIN    := $(MDXFIND_BRIDGE_LIBS_WIN)

BRIDGE_DEPS_bridge_mdxfind_NATIVE   := $(MDXFIND_ARCHIVES_NATIVE)
BRIDGE_DEPS_bridge_mdxfind_LINUX    := $(MDXFIND_ARCHIVES_LINUX)
BRIDGE_DEPS_bridge_mdxfind_WIN      := $(MDXFIND_ARCHIVES_WIN)
