#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>

static bool guest_proxy_smc(struct kvm_vcpu *vcpu)
{
	return true;
}

int pkvm_guest_smc_proxy_hyp_init(const struct pkvm_module_ops *ops)
{
	return ops->register_guest_smc_handler(guest_proxy_smc);
}
