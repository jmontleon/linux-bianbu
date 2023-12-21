#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/clk/clk-conf.h>

static int spacemit_processor_notifier(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	int cpu;
	struct device *cpu_dev;
	struct cpufreq_policy *policy = data;

	if (event == CPUFREQ_CREATE_POLICY) {
		cpu = cpumask_first(policy->related_cpus);
		cpu_dev = get_cpu_device(cpu);
		of_clk_set_defaults(cpu_dev->of_node, false);
	}

	return 0;
}

static struct notifier_block spacemit_processor_notifier_block = {
	.notifier_call = spacemit_processor_notifier,
};

static int __init spacemit_processor_driver_init(void)
{
	int ret;

	ret = cpufreq_register_notifier(&spacemit_processor_notifier_block, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("register cpufreq notifier failed\n");
		return -EINVAL;
	}

	return 0;
}

arch_initcall(spacemit_processor_driver_init);
