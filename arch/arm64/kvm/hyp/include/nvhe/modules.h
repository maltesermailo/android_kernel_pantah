#include <asm/kvm_pgtable.h>

#define HCALL_HANDLED 0
#define HCALL_UNHANDLED -1

<<<<<<< HEAD   (8fb4fb ANDROID: KVM: arm64: Temporary fix for stage2 refcounting is)
int __pkvm_register_host_smc_handler(bool (*cb)(struct user_pt_regs *));
int __pkvm_register_default_trap_handler(bool (*cb)(struct user_pt_regs *));
int __pkvm_register_illegal_abt_notifier(void (*cb)(struct user_pt_regs *));
int __pkvm_register_hyp_panic_notifier(void (*cb)(struct user_pt_regs *));
=======
int __pkvm_register_host_smc_handler(bool (*cb)(struct kvm_cpu_context *));
int __pkvm_register_default_trap_handler(bool (*cb)(struct kvm_cpu_context *));
int __pkvm_register_illegal_abt_notifier(void (*cb)(struct kvm_cpu_context *));
int __pkvm_register_hyp_panic_notifier(void (*cb)(struct kvm_cpu_context *));
int __pkvm_register_enter_exit_notifier(void (*entry)(void), void (*exit)(void));
>>>>>>> CHANGE (e9355b ANDROID: KVM: arm64: Notify pKVM modules when entering/exiti)

enum pkvm_psci_notification;
int __pkvm_register_psci_notifier(void (*cb)(enum pkvm_psci_notification, struct user_pt_regs *));

#ifdef CONFIG_MODULES
int __pkvm_init_module(void *module_init);
int __pkvm_register_hcall(unsigned long hfn_hyp_va);
int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt);
void __pkvm_close_module_registration(void);
#else
static inline int __pkvm_init_module(void *module_init) { return -EOPNOTSUPP; }
static inline int
__pkvm_register_hcall(unsigned long hfn_hyp_va) { return -EOPNOTSUPP; }
static inline int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt)
{
	return HCALL_UNHANDLED;
}
static inline void __pkvm_close_module_registration(void) { }
#endif
