#include "common.h"
#include <drm/drm.h>
#include <drm/amdgpu_drm.h>


void * alloc_and_reg_amdgpu_mem_ioctl() {
	int drm_fd = open("/dev/dri/renderD128", O_RDWR);
	if (drm_fd < 0){
		fprintf(stderr, "Error: could not open drm fd\n");
		return -1;
	}

	union drm_amdgpu_gem_create gem_create = {{}};

	gem_create.in.bo_size = 1UL << 16;

	// if using VRAM (gpu memory)
	gem_create.in.domains = AMDGPU_GEM_DOMAIN_VRAM;
	gem_create.in.domain_flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	//gem_create.in.domains = AMDGPU_GEM_DOMAIN_GTT;
	//gem_create.in.domain_flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC;


	ret = ioctl(drm_fd, DRM_IOCTL_AMDGPU_GEM_CREATE, &gem_create);
	if (ret != 0){
		fprintf(stderr, "AMDGPU create failed\n");
		return -1;
	}

	uint32_t create_handle = gem_create.out.handle;

	union drm_amdgpu_gem_mmap gem_mmap = {{}};
	gem_mmap.in.handle = create_handle;

	ret = ioctl(drm_fd, DRM_IOCTL_AMDGPU_GEM_MMAP, &gem_mmap);
	if (ret != 0){
		fprintf(stderr, "Error: ioctl GEM_MMAP failed\n");
		return -1;
	}

	uint64_t dptr = gem_mmap.out.addr_ptr;



	struct drm_prime_handle prime_handle = {};

	prime_handle.handle = create_handle;
	prime_handle.flags = O_RDWR;

	ret = ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_handle);
	if (ret != 0){
		fprintf(stderr, "PRIME_HANDLE_TO_FD failed\n");
		return -1;
	}

	int dmabuf_fd = prime_handle.fd;

	printf("\n\nDptr on Cpu: %p\nDptr: %p\n\n", dptr_on_cpu, dptr);


	// Creating VA to use for GPU
	struct drm_amdgpu_gem_va va_args = { 0 };
	va_args.handle = create_handle;
	va_args.operation = AMDGPU_VA_OP_REPLACE;
	va_args.flags = AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE;
	
	// Figure out this...?
	va_args.va_address = dptr;
	

	va_args.offset_in_bo = 0;
	va_args.map_size = gem_create.in.bo_size;


	ret = ioctl(drm_fd, DRM_IOCTL_AMDGPU_VM, &va_args);
	if (ret != 0){
		fprintf(stderr, "Error: ioctl AMDGPU_VM failed\n");
		return -1;
	}


	void * dptr_on_cpu = mmap(NULL, 1UL << 16, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
	if (dptr_on_cpu == NULL){
		fprintf(stderr, "Error: mmap failed\n");
		return -1;
	}
	
	
	// printf("Attempting to register DMA buf with verbs...\n");

	int mr_access = IBV_ACCESS_LOCAL_WRITE; 

	mr = ibv_reg_dmabuf_mr(pd, 0, 1UL << 16, dptr_on_cpu, dmabuf_fd, mr_access);
	if (mr == NULL){
		fprintf(stderr, "Error: ibv_reg_dmabuf_mr failed\n");
		return -1;
	}
}