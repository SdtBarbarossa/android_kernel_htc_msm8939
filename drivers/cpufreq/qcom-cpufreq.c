/* drivers/cpufreq/qcom-cpufreq.c
 *
 * MSM architecture cpufreq driver
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2015, The Linux Foundation. All rights reserved.
 * Author: Mike A. Chan <mikechan@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/suspend.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <trace/events/power.h>

#if defined(CONFIG_HTC_DEBUG_FOOTPRINT)
#include <htc_mnemosyne/htc_footprint.h>
#endif

static DEFINE_MUTEX(l2bw_lock);

static struct clk *cpu_clk[NR_CPUS];
static struct clk *l2_clk;
static DEFINE_PER_CPU(struct cpufreq_frequency_table *, freq_table);
static bool hotplug_ready;

struct cpufreq_suspend_t {
	struct mutex suspend_mutex;
	int device_suspended;
};

static DEFINE_PER_CPU(struct cpufreq_suspend_t, cpufreq_suspend);

static int set_cpu_freq(struct cpufreq_policy *policy, unsigned int new_freq,
			unsigned int index)
{
	int ret = 0;
	struct cpufreq_freqs freqs;
	unsigned long rate;

	freqs.old = policy->cur;
	freqs.new = new_freq;
	freqs.cpu = policy->cpu;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	trace_cpu_frequency_switch_start(freqs.old, freqs.new, policy->cpu);

	rate = new_freq * 1000;
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_acpuclk_footprint_by_clk(cpu_clk[policy->cpu], ACPU_ENTER);
	set_acpuclk_cpu_freq_footprint_by_clk(FT_PREV_RATE, cpu_clk[policy->cpu], policy->cur * 1000);
	set_acpuclk_cpu_freq_footprint_by_clk(FT_NEW_RATE, cpu_clk[policy->cpu], rate);
#endif
	rate = clk_round_rate(cpu_clk[policy->cpu], rate);
	ret = clk_set_rate(cpu_clk[policy->cpu], rate);
	if (!ret) {
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
		set_acpuclk_footprint_by_clk(cpu_clk[policy->cpu], ACPU_BEFORE_UPDATE_L2_BW);
#endif
		cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
		trace_cpu_frequency_switch_end(policy->cpu);
	}

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_acpuclk_footprint_by_clk(cpu_clk[policy->cpu], ACPU_LEAVE);
#endif

	return ret;
}

static int msm_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	int ret = 0;
	int index;
	struct cpufreq_frequency_table *table;

	mutex_lock(&per_cpu(cpufreq_suspend, policy->cpu).suspend_mutex);

	if (target_freq == policy->cur)
		goto done;

	if (per_cpu(cpufreq_suspend, policy->cpu).device_suspended) {
		pr_debug("cpufreq: cpu%d scheduling frequency change "
				"in suspend.\n", policy->cpu);
		ret = -EFAULT;
		goto done;
	}

	table = cpufreq_frequency_get_table(policy->cpu);
	if (cpufreq_frequency_table_target(policy, table, target_freq, relation,
			&index)) {
		pr_err("cpufreq: invalid target_freq: %d\n", target_freq);
		ret = -EINVAL;
		goto done;
	}

	pr_debug("CPU[%d] target %d relation %d (%d-%d) selected %d\n",
		policy->cpu, target_freq, relation,
		policy->min, policy->max, table[index].frequency);

	ret = set_cpu_freq(policy, table[index].frequency,
			   table[index].driver_data);
done:
	mutex_unlock(&per_cpu(cpufreq_suspend, policy->cpu).suspend_mutex);
	return ret;
}

static int msm_cpufreq_verify(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
			policy->cpuinfo.max_freq);
	return 0;
}

static unsigned int msm_cpufreq_get_freq(unsigned int cpu)
{
	return clk_get_rate(cpu_clk[cpu]) / 1000;
}

static int msm_cpufreq_init(struct cpufreq_policy *policy)
{
	int cur_freq;
	int index;
	int ret = 0;
	struct cpufreq_frequency_table *table =
			per_cpu(freq_table, policy->cpu);
	int cpu;

	for_each_possible_cpu(cpu)
		if (cpu_clk[cpu] == cpu_clk[policy->cpu])
			cpumask_set_cpu(cpu, policy->cpus);

	if (cpufreq_frequency_table_cpuinfo(policy, table))
		pr_err("cpufreq: failed to get policy min/max\n");

	cur_freq = clk_get_rate(cpu_clk[policy->cpu])/1000;

	if (cpufreq_frequency_table_target(policy, table, cur_freq,
	    CPUFREQ_RELATION_H, &index) &&
	    cpufreq_frequency_table_target(policy, table, cur_freq,
	    CPUFREQ_RELATION_L, &index)) {
		pr_info("cpufreq: cpu%d at invalid freq: %d\n",
				policy->cpu, cur_freq);
		return -EINVAL;
	}
	ret = set_cpu_freq(policy, table[index].frequency,
			   table[index].driver_data);
	if (ret)
		return ret;
	pr_debug("cpufreq: cpu%d init at %d switching to %d\n",
			policy->cpu, cur_freq, table[index].frequency);
	policy->cur = table[index].frequency;
	cpufreq_frequency_table_get_attr(table, policy->cpu);

	return 0;
}

static int msm_cpufreq_cpu_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	int rc;

	
	if (!hotplug_ready)
		return NOTIFY_BAD;

	switch (action & ~CPU_TASKS_FROZEN) {

	case CPU_DYING:
		clk_disable(cpu_clk[cpu]);
		clk_disable(l2_clk);
		break;
	case CPU_DEAD:
		clk_unprepare(cpu_clk[cpu]);
		clk_unprepare(l2_clk);
		break;
	case CPU_UP_CANCELED:
		clk_unprepare(cpu_clk[cpu]);
		clk_unprepare(l2_clk);
		break;
	case CPU_UP_PREPARE:
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
		set_hotplug_on_footprint(cpu, HOF_ENTER);
#endif
		rc = clk_prepare(l2_clk);
		if (rc < 0)
			return NOTIFY_BAD;
		rc = clk_prepare(cpu_clk[cpu]);
		if (rc < 0) {
			clk_unprepare(l2_clk);
			return NOTIFY_BAD;
		}
		break;

	case CPU_STARTING:
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
		set_hotplug_on_footprint(cpu, HOF_BEFORE_UPDATE_L2_BW);
#endif
		rc = clk_enable(l2_clk);
		if (rc < 0)
			return NOTIFY_BAD;
		rc = clk_enable(cpu_clk[cpu]);
		if (rc) {
			clk_disable(l2_clk);
			return NOTIFY_BAD;
		}
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
		set_hotplug_on_footprint(cpu, HOF_LEAVE);
#endif
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata msm_cpufreq_cpu_notifier = {
	.notifier_call = msm_cpufreq_cpu_callback,
};

static int msm_cpufreq_suspend(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(cpufreq_suspend, cpu).suspend_mutex);
		per_cpu(cpufreq_suspend, cpu).device_suspended = 1;
		mutex_unlock(&per_cpu(cpufreq_suspend, cpu).suspend_mutex);
	}

	return NOTIFY_DONE;
}

static int msm_cpufreq_resume(void)
{
	int cpu, ret;
	struct cpufreq_policy policy;

	for_each_possible_cpu(cpu) {
		per_cpu(cpufreq_suspend, cpu).device_suspended = 0;
	}

	get_online_cpus();
	for_each_online_cpu(cpu) {
		ret = cpufreq_get_policy(&policy, cpu);
		if (ret)
			continue;
		if (policy.cur <= policy.max && policy.cur >= policy.min)
			continue;
		ret = cpufreq_update_policy(cpu);
		if (ret)
			pr_info("cpufreq: Current frequency violates policy min/max for CPU%d\n",
			       cpu);
		else
			pr_info("cpufreq: Frequency violation fixed for CPU%d\n",
				cpu);
	}
	put_online_cpus();

	return NOTIFY_DONE;
}

static int msm_cpufreq_pm_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	switch (event) {
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		return msm_cpufreq_resume();
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		return msm_cpufreq_suspend();
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block msm_cpufreq_pm_notifier = {
	.notifier_call = msm_cpufreq_pm_event,
};

static struct freq_attr *msm_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver msm_cpufreq_driver = {
	
	.flags		= CPUFREQ_STICKY | CPUFREQ_CONST_LOOPS,
	.init		= msm_cpufreq_init,
	.verify		= msm_cpufreq_verify,
	.target		= msm_cpufreq_target,
	.get		= msm_cpufreq_get_freq,
	.name		= "msm",
	.attr		= msm_freq_attr,
};

static struct cpufreq_frequency_table *cpufreq_parse_dt(struct device *dev,
						char *tbl_name, int cpu)
{
	int ret, nf, i;
	u32 *data;
	struct cpufreq_frequency_table *ftbl;

	
	if (!of_find_property(dev->of_node, tbl_name, &nf))
		return ERR_PTR(-EINVAL);
	nf /= sizeof(*data);

	if (nf == 0)
		return ERR_PTR(-EINVAL);

	data = devm_kzalloc(dev, nf * sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_u32_array(dev->of_node, tbl_name, data, nf);
	if (ret)
		return ERR_PTR(ret);

	ftbl = devm_kzalloc(dev, (nf + 1) * sizeof(*ftbl), GFP_KERNEL);
	if (!ftbl)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nf; i++) {
		unsigned long f;

		f = clk_round_rate(cpu_clk[cpu], data[i] * 1000);
		if (IS_ERR_VALUE(f))
			break;
		f /= 1000;

		if (i > 0 && f <= ftbl[i-1].frequency)
			break;

		ftbl[i].driver_data = i;
		ftbl[i].frequency = f;
	}

	ftbl[i].driver_data = i;
	ftbl[i].frequency = CPUFREQ_TABLE_END;

	devm_kfree(dev, data);

	return ftbl;
}

static int __init msm_cpufreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	char clk_name[] = "cpu??_clk";
	char tbl_name[] = "qcom,cpufreq-table-??";
	struct clk *c;
	int cpu;
	struct cpufreq_frequency_table *ftbl;

	l2_clk = devm_clk_get(dev, "l2_clk");
	if (IS_ERR(l2_clk))
		l2_clk = NULL;

	for_each_possible_cpu(cpu) {
		snprintf(clk_name, sizeof(clk_name), "cpu%d_clk", cpu);
		c = devm_clk_get(dev, clk_name);
		if (IS_ERR(c))
			return PTR_ERR(c);
		cpu_clk[cpu] = c;
	}
	hotplug_ready = true;

	
	if (of_property_read_bool(dev->of_node, "qcom,governor-per-policy"))
		msm_cpufreq_driver.flags |= CPUFREQ_HAVE_GOVERNOR_PER_POLICY;

	
	ftbl = cpufreq_parse_dt(dev, "qcom,cpufreq-table", 0);
	if (!IS_ERR(ftbl)) {
		for_each_possible_cpu(cpu) {
			per_cpu(freq_table, cpu) = ftbl;
			cpufreq_frequency_table_get_attr(ftbl, cpu);
		}
		return 0;
	}

	for_each_possible_cpu(cpu) {
		snprintf(tbl_name, sizeof(tbl_name),
			 "qcom,cpufreq-table-%d", cpu);
		ftbl = cpufreq_parse_dt(dev, tbl_name, cpu);

		
		if (cpu == 0 && IS_ERR(ftbl)) {
			dev_err(dev, "Failed to parse CPU0's freq table\n");
			return PTR_ERR(ftbl);
		}
		if (cpu == 0) {
			per_cpu(freq_table, cpu) = ftbl;
			cpufreq_frequency_table_get_attr(ftbl, cpu);
			continue;
		}

		if (cpu_clk[cpu] != cpu_clk[cpu - 1] && IS_ERR(ftbl)) {
			dev_err(dev, "Failed to parse CPU%d's freq table\n",
				cpu);
			return PTR_ERR(ftbl);
		}

		
		if (cpu_clk[cpu] == cpu_clk[cpu - 1]) {
			if (!IS_ERR(ftbl)) {
				dev_warn(dev, "Conflicting tables for CPU%d\n",
					 cpu);
				kfree(ftbl);
			}
			ftbl = per_cpu(freq_table, cpu - 1);
		}
		per_cpu(freq_table, cpu) = ftbl;
		cpufreq_frequency_table_get_attr(ftbl, cpu);
	}

	return 0;
}

static struct of_device_id match_table[] = {
	{ .compatible = "qcom,msm-cpufreq" },
	{}
};

static struct platform_driver msm_cpufreq_plat_driver = {
	.driver = {
		.name = "msm-cpufreq",
		.of_match_table = match_table,
		.owner = THIS_MODULE,
	},
};

static int __init msm_cpufreq_register(void)
{
	int cpu, rc;

	for_each_possible_cpu(cpu) {
		mutex_init(&(per_cpu(cpufreq_suspend, cpu).suspend_mutex));
		per_cpu(cpufreq_suspend, cpu).device_suspended = 0;
	}

	rc = platform_driver_probe(&msm_cpufreq_plat_driver,
				   msm_cpufreq_probe);
	if (rc < 0) {
		
		unregister_hotcpu_notifier(&msm_cpufreq_cpu_notifier);
		for_each_possible_cpu(cpu)
			mutex_destroy(&(per_cpu(cpufreq_suspend, cpu).
					suspend_mutex));
		return rc;
	}

	register_pm_notifier(&msm_cpufreq_pm_notifier);
	cpu_hotplug_disable();
	rc = cpufreq_register_driver(&msm_cpufreq_driver);
	cpu_hotplug_enable();

	return rc;
}

int is_sync_cpu(struct cpumask *mask, int first_cpu)
{
	int cpu;

	for_each_cpu_and(cpu, mask, cpu_possible_mask) {
		if (cpu == first_cpu)
			continue;
		if (cpu_clk[cpu] != cpu_clk[first_cpu])
			return 0;
	}

	return 1;
}

EXPORT_SYMBOL(is_sync_cpu);

subsys_initcall(msm_cpufreq_register);

static int __init msm_cpufreq_early_register(void)
{
	return register_hotcpu_notifier(&msm_cpufreq_cpu_notifier);
}
core_initcall(msm_cpufreq_early_register);
