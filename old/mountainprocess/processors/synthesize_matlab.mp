{
	"processors":[
		{
			"name":"synthesize_timeseries_001_matlab",
			"version":"0.13",
			"description":"",
			"inputs":[],
			"outputs":[
				{"name":"waveforms"},
				{"name":"timeseries"},
				{"name":"firings_true"}
			],
			"parameters":[
				{"name":"M","optional":false},{"name":"T","optional":false},{"name":"K","optional":false},
				{"name":"duration","optional":false},{"name":"noise_level","optional":false},
				{"name":"firing_rate_min","optional":false},{"name":"firing_rate_max","optional":false},
				{"name":"amp_variation_min","optional":false},{"name":"amp_variation_max","optional":false}
			],
			"exe_command":"$(basepath)/run_matlab.sh $(basepath)/synthesize_matlab \"opts=struct('M',$M$,'T',$T$,'K',$K$,'duration',$duration$,'noise_level',$noise_level$,'firing_rate_range',[$firing_rate_min$,$firing_rate_max$],'amp_variation_range',[$amp_variation_min$,$amp_variation_max$]); synthesize_timeseries_001('$timeseries$','$firings_true$','$waveforms$',opts);\""
		}
	]
}
