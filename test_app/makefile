test_app: memory-read-write.cpp ../approx.cpp
	g++ -O3 -o memory-read-write memory-read-write.cpp ../approx.cpp
	#$(PIN_ROOT)/pin -t obj-intel64/memapprox-efficient-bitdepth.so -rd_ber "1E-02" -wr_ber "1E-02" -bt_dth "10" -- ./memory-read-write
