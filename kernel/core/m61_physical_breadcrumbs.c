#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/boot_protocol.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu_inventory.h>
#include <boring/exception.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/graphics.h>
#include <boring/heap.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/irq.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/syscall_test.h>
#include <boring/timer.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>
#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 physical trace must stay candidate-build gated"
#endif
#define TX 8ULL
#define SY 42ULL
#define SH 12ULL
#define SCALE 2U
#define LAST_STAGE 27U
const char boring_m61_physical_breadcrumbs_enabled[]="M61 direct framebuffer text boot trace enabled";
static const struct boring_framebuffer *fb;
static bool serial_ready,wm_ready,desktop_presented;
struct glyph { char c; uint16_t b; };
static const struct glyph glyphs[]={
 {'A',0x2bedU},{'B',0x6baeU},{'C',0x3923U},{'D',0x6b6eU},{'E',0x79a7U},{'F',0x79a4U},{'G',0x396bU},{'H',0x5bedU},{'I',0x7497U},{'J',0x126aU},{'K',0x5badU},{'L',0x4927U},{'M',0x5fedU},{'N',0x5ffdU},{'O',0x2b6aU},{'P',0x6ba4U},{'Q',0x2b7bU},{'R',0x6badU},{'S',0x388eU},{'T',0x7492U},{'U',0x5b6fU},{'V',0x5b6aU},{'W',0x5bfdU},{'X',0x5aadU},{'Y',0x5a92U},{'Z',0x72a7U},
 {'0',0x7b6fU},{'1',0x2c97U},{'2',0x62a7U},{'3',0x628eU},{'4',0x5bc9U},{'5',0x798eU},{'6',0x39aaU},{'7',0x7292U},{'8',0x2aaaU},{'9',0x2aceU},{'[',0x6926U},{']',0x324bU},{'>',0x4454U},{'+',0x05d0U},{'!',0x2482U},{'=',0x0e38U},{':',0x0410U},{'/',0x12a4U},{'-',0x01c0U},{'.',0x0002U},{'_',0x0007U},{' ',0U}
};
static uint16_t bits(char c){size_t i;if(c>='a'&&c<='z')c=(char)(c-('a'-'A'));for(i=0U;i<sizeof(glyphs)/sizeof(glyphs[0]);++i)if(glyphs[i].c==c)return glyphs[i].b;return 0U;}
static uint32_t color(uint8_t r,uint8_t g,uint8_t b){return fb==NULL?0U:boring_color_pack(fb,r,g,b);}
static void chr(uint64_t x,uint64_t y,char c,uint32_t col,uint32_t scale){uint16_t g=bits(c);uint32_t row;if(fb==NULL||scale==0U)return;for(row=0U;row<5U;++row){uint32_t column;for(column=0U;column<3U;++column){uint32_t bit=((4U-row)*3U)+(2U-column);if((g&(uint16_t)(1U<<bit))!=0U){uint32_t yy;for(yy=0U;yy<scale;++yy){uint32_t xx;for(xx=0U;xx<scale;++xx)(void)boring_graphics_put_pixel(fb,x+(uint64_t)(column*scale+xx),y+(uint64_t)(row*scale+yy),col);}}}}}
static void text(uint64_t x,uint64_t y,const char *s,uint32_t col,uint32_t scale){size_t i=0U;uint64_t step=(uint64_t)(4U*scale);if(fb==NULL||s==NULL)return;while(s[i]!='\0'&&i<96U){chr(x,y,s[i],col,scale);x+=step;++i;}}
static uint64_t dec(uint64_t x,uint64_t y,uint64_t v,uint32_t col,uint32_t scale){char d[21];size_t n=0U;do{d[n++]=(char)('0'+(char)(v%10ULL));v/=10ULL;}while(v!=0ULL&&n<sizeof(d));while(n!=0U){chr(x,y,d[--n],col,scale);x+=(uint64_t)(4U*scale);}return x;}
static void hex64(uint64_t x,uint64_t y,uint64_t v,uint32_t col,uint32_t scale){static const char h[]="0123456789ABCDEF";int shift;text(x,y,"0X",col,scale);x+=(uint64_t)(8U*scale);for(shift=60;shift>=0;shift-=4){chr(x,y,h[(v>>(uint32_t)shift)&0x0fULL],col,scale);x+=(uint64_t)(4U*scale);}}
static uint64_t stage_y(uint32_t n){return SY+((uint64_t)(n-1U)*SH);}
static void serial_stage(uint32_t n,char mark,const char *label){if(!serial_ready)return;serial_write_string("M61 TRACE [");if(n<10U)serial_write_string("0");serial_write_u64((uint64_t)n);serial_write_string(mark=='>'?">] ":mark=='+'?"+] ":"!] ");serial_write_string(label);serial_write_string("\n");}
static void stage(uint32_t n,char mark,const char *label){uint64_t x;uint32_t col;if(fb==NULL||n==0U||n>LAST_STAGE||label==NULL)return;(void)boring_graphics_fill_rect(fb,4ULL,stage_y(n)-1ULL,fb->width>8ULL?fb->width-8ULL:0ULL,11ULL,color(12U,12U,16U));col=mark=='>'?color(255U,190U,64U):mark=='+'?color(96U,255U,128U):color(255U,80U,80U);x=TX;chr(x,stage_y(n),'[',col,SCALE);x+=8ULL;if(n<10U){chr(x,stage_y(n),'0',col,SCALE);x+=8ULL;}x=dec(x,stage_y(n),(uint64_t)n,col,SCALE);chr(x,stage_y(n),mark,col,SCALE);x+=8ULL;chr(x,stage_y(n),']',col,SCALE);x+=16ULL;text(x,stage_y(n),label,col,SCALE);serial_stage(n,mark,label);}
static void acquire(void){enum boring_framebuffer_status st=boring_framebuffer_boot_init();uint32_t dim;if(st!=BORING_FRAMEBUFFER_STATUS_READY)return;fb=boring_framebuffer_get();if(!boring_framebuffer_surface_valid(fb)){fb=NULL;return;}(void)boring_graphics_clear(fb,color(12U,12U,16U));text(TX,8ULL,"BORINGOS PHYSICAL BOOT TRACE",color(96U,224U,255U),SCALE);dim=color(180U,180U,190U);text(TX,22ULL,"FB ",dim,1U);uint64_t x=TX+12ULL;x=dec(x,22ULL,fb->width,dim,1U);text(x,22ULL,"X",dim,1U);x+=4ULL;x=dec(x,22ULL,fb->height,dim,1U);text(x,22ULL," PITCH=",dim,1U);x+=28ULL;x=dec(x,22ULL,fb->pitch,dim,1U);text(x,22ULL," BPP=",dim,1U);x+=20ULL;(void)dec(x,22ULL,(uint64_t)fb->bpp,dim,1U);}
static bool ends(const char *s,const char *e){size_t ns=0U,ne=0U,i;if(s==NULL||e==NULL)return false;while(s[ns]!='\0'&&ns<96U)++ns;while(e[ne]!='\0'&&ne<96U)++ne;if(ns>=96U||ne>=96U||ns<ne)return false;for(i=0U;i<ne;++i)if(s[ns-ne+i]!=e[i])return false;return true;}
static bool same(const char *s,size_t n,const char *e,size_t en){size_t i;if(s==NULL||e==NULL||n!=en)return false;for(i=0U;i<n;++i)if(s[i]!=e[i])return false;return true;}
static void present_guard(uint32_t n,const char *label){stage(n,'>',label);if(fb!=NULL&&fb->height>=14ULL){uint64_t y=fb->height-12ULL;text(TX,y,"PRESENT IN PROGRESS",color(255U,190U,64U),SCALE);}}
static void fatal(const struct x86_64_trap_frame *f){uint32_t c;if(fb==NULL)return;(void)boring_graphics_clear(fb,color(96U,0U,0U));c=color(255U,240U,240U);text(TX,20ULL,"FATAL",c,SCALE);if(f==NULL){text(TX,44ULL,"TRAP FRAME UNAVAILABLE",c,SCALE);return;}text(TX,44ULL,"VECTOR=",c,SCALE);(void)dec(TX+56ULL,44ULL,f->vector,c,SCALE);text(TX,58ULL,"ERROR=",c,SCALE);hex64(TX+48ULL,58ULL,f->error_code,c,SCALE);text(TX,72ULL,"RIP=",c,SCALE);hex64(TX+32ULL,72ULL,f->rip,c,SCALE);}
void __real_serial_init(void);void __wrap_serial_init(void);
void __real_boring_cpu_inventory_init(void);void __wrap_boring_cpu_inventory_init(void);
void __real_boring_pci_inventory_init(void);void __wrap_boring_pci_inventory_init(void);
void __real_boring_smbios_boot_init(const struct boring_limine_hhdm_response *,const struct boring_limine_memmap_response *);void __wrap_boring_smbios_boot_init(const struct boring_limine_hhdm_response *,const struct boring_limine_memmap_response *);
bool __real_pmm_init(const struct boring_limine_memmap_response *);bool __wrap_pmm_init(const struct boring_limine_memmap_response *);
bool __real_vmm_init(const struct boring_limine_hhdm_response *,const struct boring_limine_paging_mode_response *,const struct boring_limine_memmap_response *);bool __wrap_vmm_init(const struct boring_limine_hhdm_response *,const struct boring_limine_paging_mode_response *,const struct boring_limine_memmap_response *);
bool __real_heap_init(void);bool __wrap_heap_init(void);bool __real_exception_init(void);bool __wrap_exception_init(void);void __real_syscall_test_run(void);void __wrap_syscall_test_run(void);
bool __real_boring_input_init(void);bool __wrap_boring_input_init(void);bool __real_irq_init(void);bool __wrap_irq_init(void);bool __real_timer_init(uint32_t);bool __wrap_timer_init(uint32_t);
bool __real_xhci_init(struct xhci_state *);bool __wrap_xhci_init(struct xhci_state *);bool __real_xhci_address_connected(struct xhci_state *);bool __wrap_xhci_address_connected(struct xhci_state *);bool __real_xhci_discover_descriptors(struct xhci_state *);bool __wrap_xhci_discover_descriptors(struct xhci_state *);bool __real_xhci_configure_hid_devices_mixed(struct xhci_state *);bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *);bool __real_usb_mass_storage_init(struct xhci_state *);bool __wrap_usb_mass_storage_init(struct xhci_state *);
enum vfs_result __real_boringfs_vfs_create_writable(const struct block_device *,uint64_t,struct boringfs_vfs **,struct boringfs_validation_error *);enum vfs_result __wrap_boringfs_vfs_create_writable(const struct block_device *,uint64_t,struct boringfs_vfs **,struct boringfs_validation_error *);
bool __real_process_set_name(struct process *,const char *);bool __wrap_process_set_name(struct process *,const char *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_claim(uint64_t,struct boring_display_scanout_info *);enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(uint64_t,struct boring_display_scanout_info *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(struct process *,uint32_t);enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(struct process *,uint32_t);
enum boring_ipc_result __real_boring_ipc_service_register(struct process *,const char *,size_t,uint32_t *);enum boring_ipc_result __wrap_boring_ipc_service_register(struct process *,const char *,size_t,uint32_t *);
void __real_x86_64_exception_dispatch(const struct x86_64_trap_frame *);void __wrap_x86_64_exception_dispatch(const struct x86_64_trap_frame *);
void __wrap_serial_init(void){acquire();stage(1U,'>',"SERIAL PROBE");__real_serial_init();serial_ready=true;stage(1U,'+',"SERIAL PROBE");}
void __wrap_boring_cpu_inventory_init(void){stage(2U,'>',"CPU INVENTORY");__real_boring_cpu_inventory_init();stage(2U,'+',"CPU INVENTORY");}
void __wrap_boring_pci_inventory_init(void){stage(3U,'>',"PCI INVENTORY");__real_boring_pci_inventory_init();stage(3U,'+',"PCI INVENTORY");}
void __wrap_boring_smbios_boot_init(const struct boring_limine_hhdm_response *h,const struct boring_limine_memmap_response *m){stage(4U,'>',"SMBIOS");__real_boring_smbios_boot_init(h,m);stage(4U,'+',"SMBIOS");}
bool __wrap_pmm_init(const struct boring_limine_memmap_response *m){bool r;stage(5U,'>',"PMM INIT");r=__real_pmm_init(m);stage(5U,r?'+':'!',"PMM INIT");return r;}
bool __wrap_vmm_init(const struct boring_limine_hhdm_response *h,const struct boring_limine_paging_mode_response *p,const struct boring_limine_memmap_response *m){bool r;stage(6U,'>',"VMM INIT");r=__real_vmm_init(h,p,m);stage(6U,r?'+':'!',"VMM INIT");return r;}
bool __wrap_heap_init(void){bool r;stage(7U,'>',"HEAP INIT");r=__real_heap_init();stage(7U,r?'+':'!',"HEAP INIT");return r;}
bool __wrap_exception_init(void){bool r;stage(8U,'>',"EXCEPTIONS");r=__real_exception_init();stage(8U,r?'+':'!',"EXCEPTIONS");return r;}
void __wrap_syscall_test_run(void){stage(9U,'>',"DESKTOP HARNESS");__real_syscall_test_run();stage(9U,'+',"DESKTOP HARNESS");}
bool __wrap_boring_input_init(void){bool r;stage(10U,'>',"INPUT CORE");r=__real_boring_input_init();stage(10U,r?'+':'!',"INPUT CORE");return r;}
bool __wrap_irq_init(void){bool r;stage(11U,'>',"IRQ INIT");r=__real_irq_init();stage(11U,r?'+':'!',"IRQ INIT");return r;}
bool __wrap_timer_init(uint32_t hz){bool r;stage(12U,'>',"PIT TIMER");r=__real_timer_init(hz);stage(12U,r?'+':'!',"PIT TIMER");return r;}
bool __wrap_xhci_init(struct xhci_state *s){bool r;stage(13U,'>',"XHCI CONTROLLER");r=__real_xhci_init(s);stage(13U,r?'+':'!',"XHCI CONTROLLER");return r;}
bool __wrap_xhci_address_connected(struct xhci_state *s){bool r;stage(14U,'>',"XHCI ADDRESS");r=__real_xhci_address_connected(s);stage(14U,r?'+':'!',"XHCI ADDRESS");return r;}
bool __wrap_xhci_discover_descriptors(struct xhci_state *s){bool r;stage(15U,'>',"XHCI DESCRIPTORS");r=__real_xhci_discover_descriptors(s);stage(15U,r?'+':'!',"XHCI DESCRIPTORS");return r;}
bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *s){bool r;stage(16U,'>',"XHCI HID CONFIG");r=__real_xhci_configure_hid_devices_mixed(s);stage(16U,r?'+':'!',"XHCI HID CONFIG");return r;}
bool __wrap_usb_mass_storage_init(struct xhci_state *s){bool r;stage(17U,'>',"USB MASS STORAGE");r=__real_usb_mass_storage_init(s);stage(17U,r?'+':'!',"USB MASS STORAGE");return r;}
enum vfs_result __wrap_boringfs_vfs_create_writable(const struct block_device *d,uint64_t id,struct boringfs_vfs **out,struct boringfs_validation_error *err){enum vfs_result r;stage(18U,'>',"BORINGFS ROOT");r=__real_boringfs_vfs_create_writable(d,id,out,err);stage(18U,r==VFS_RESULT_OK?'+':'!',"BORINGFS ROOT");return r;}
bool __wrap_process_set_name(struct process *p,const char *name){uint32_t n=0U;const char *l=NULL;bool r;if(ends(name,"boring-init")){n=19U;l="BORING-INIT START";}else if(ends(name,"boring-display")){n=20U;l="BORING-DISPLAY START";}else if(ends(name,"boringwm")){n=24U;l="BORING-WM START";}else if(ends(name,"boring-terminal")){n=27U;l="TERMINAL START";}if(n!=0U)stage(n,'>',l);r=__real_process_set_name(p,name);if(n!=0U)stage(n,r?'+':'!',l);return r;}
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(uint64_t pid,struct boring_display_scanout_info *info){enum boring_framebuffer_user_result r;stage(21U,'>',"DISPLAY FB CLAIM");r=__real_boring_framebuffer_user_claim(pid,info);stage(21U,r==BORING_FRAMEBUFFER_USER_OK?'+':'!',"DISPLAY FB CLAIM");return r;}
enum boring_ipc_result __wrap_boring_ipc_service_register(struct process *p,const char *name,size_t len,uint32_t *out){uint32_t n=0U;const char *l=NULL;enum boring_ipc_result r;if(same(name,len,"boring.display",14U)){n=22U;l="DISPLAY SERVICE";}else if(same(name,len,"boring.wm",9U)){n=25U;l="WM SERVICE";}if(n!=0U)stage(n,'>',l);r=__real_boring_ipc_service_register(p,name,len,out);if(n!=0U)stage(n,r==BORING_IPC_RESULT_OK?'+':'!',l);if(n==25U&&r==BORING_IPC_RESULT_OK)wm_ready=true;return r;}
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(struct process *p,uint32_t h){enum boring_framebuffer_user_result r;uint32_t n;const char *l;if(desktop_presented)return __real_boring_framebuffer_user_present(p,h);n=wm_ready?26U:23U;l=wm_ready?"DESKTOP PRESENT":"DISPLAY INITIAL PRESENT";present_guard(n,l);r=__real_boring_framebuffer_user_present(p,h);if(r!=BORING_FRAMEBUFFER_USER_OK){stage(n,'!',l);return r;}if(wm_ready){desktop_presented=true;serial_stage(n,'+',l);}else stage(n,'+',l);return r;}
void __wrap_x86_64_exception_dispatch(const struct x86_64_trap_frame *f){fatal(f);__real_x86_64_exception_dispatch(f);}
