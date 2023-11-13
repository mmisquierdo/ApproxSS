# ApproxSS

ApproxSS (Approximate Storage Simulator) is a simulator developed for error resilience evaluation and energy consumption estimation of systems that employ approximate memory. During a target application execution, ApproxSS performs random error injections into the contents of read and write operations performed on specified buffers, in order to replicate the impact of using approximate storage. Besides that, it also estimates the energy consumption of such buffers based on the memory accesses to which they are subjected. Such error injections are made through pseudorandom bit flips controlled by Bit Error Rates (BERs) and limited by a BitDepth, both informed by the user.

BERs control the probabilities of a bit being flipped, either from 0 to 1 or 1 to 0. BERs range from 0.0 (inclusive), where no bits will be changed, to 1.0 (non-inclusive), where basically every bit will be changed. Read, write, and hold operations can be subjected to different BERs and each one of them can be subjected to several BERs per in a single injection configuration. BitDepth in turn limit up to which bit of an element accessed from memory error injections can occur.

ApproxSS operates on continuous ranges of memory addresses, which, when instrumented, become approximate buffers. The instrumentation of the buffers and the control of the injection of errors is done through instrumentation markers added by the user in the source code of the target application. These markers are dummy functions, but which, when intercepted by the ApproxSS, cause the insertion of control functions. 

ApproxSS can have multiple approximate buffers simultaneously, each with a different injector configuration. To allow for later estimation of energy consumption, approximate buffers keep individual records of accesses suffered by the address ranges contained therein. Such records may have separations by periods. Periods serve as user-defined “timestamps”, providing a greater control and better granularity on access and energy data collected. 

### Energy Consumption Estimation

ApproxSS can also estimate the energy consumption of approximate buffer, based on measurements collected about memory accesses. 
Each input energy consumption profile present must have the identifier of the error injection configuration to which it corresponds and consumption values for reading, writing, and hold operations, measured in picojoules (pJ) per byte. Each BER of the error injector configuration needs an equivalent consumption value.
In addition to the consumption values for accesses that simulate approximate storage, reference consumption values are also allowed, representing the consumption of precise accesses. 
If reference values are informed, the algorithm is also able to calculate reductions in consumption. 

Consumption is estimated for each active period of each approximate buffer.
For each period, the product between the number of bytes manipulated by each operation and its energy consumption based on its respective BER is calculated. Manipulated bytes only account for bits under the BitDepth (subjected to error injection). So, if the BitDepth is smaller than the element size, this difference will be ignored in the estimation. Furthermore, in the case of hold operations (passive consumption), all buffer elements are considered (once) per period, so more periods result in bigger consumption.
Finally, the approximate total buffer consumption per operation is calculated by adding those for each period.


## Dependencies

1. [Pin](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html), a dynamic binary instrumentation framework developed by Intel with support to various Instruction Set Architectures (ISAs), operating systems, and compilers. ApproxSS was built on top of Pin, as a pintool. Preferably version 3.28 or newer. 
2. A C++17-compatible GCC compiler, preferably [GCC 12.3](https://gcc.gnu.org/) or newer.

## Compiling

To compile ApproxSS, the user must change _PIN_ROOT_ (source/make) to point to Pin's base folder. After that, using the command line

``` 
make
```

inside the subfolder /source will generate a shared object, called memapprox-effient-bitdepth.so, output into subfolder /source/obj-intel64.

### Compiling Options

In order to try to reduce as much as possible the overhead imposed by using ApproxSS, some compilation preprocessing directives were added. These directives limit or expand ApproxSS capabilities in some contexts that may not always be the user's intended use, so they are made optional. They are available to be changed in the source/compilation-option.h file.

1. MULTIPLE_ACTIVE_BUFFERS: When enabled, allows ApproxSS to keep multiple approximate buffers active simultaneously. Deactivating this flag simplifies the verification and reduces the overhead. 

2. MULTIPLE_BER_CONFIGURATION: When enabled, allows every injector configuration to have multiple BERs per error category. The exchange between them is performed by the _next_period()_ instrumentation marker, which advances the BER indices.

3. MULTIPLE__BER_ELEMENT: TO WRITE

4. LOG_FAULTS: When enabled, allows bit-level accounting error injected per category and period. Can be activated as a debugging measure or for other purposes. Writes that are overwritten before being read are not injected and, therefore, are not accounted.

5. ENABLE_PASSIVE_INJECTION: When enabled, allows permanent error injection over time. This attempts to replicate the behavior of errors caused by wear-out errors, caused by unsafe hold opearations, due to extended data refresh interval DRAM, for example. **NOTE: This type of error is applied once every period during the first read, thus may have limited fidelity**.

6. OVERCHARGE_BER: TO WRITE

7. OVERCHARGE_FLIP_BACK: When enabled, adds safety to passive error injections. Under some configurations, many periods passive BERs may be stacked in order to speed up injection. This may cause the error rate to exceed 1.0 and the simulator to overlook injections that could otherwise happen. Thus, this option adds extra checks to make sure these errors are better simulated.

8. NARROW_ACCESS_INSTRUMENTATION: By default, due to Pin limitations that make it impossible for it to know the effective address of the accesses made by instructions at instrumentation time, all memory accesses made by the target application are instrumented. This forces the ApproxSS to check if they belong to some approximate buffer every time are executed, causing overhead. When enabled, allows accesses to be instrumented only at user-specified times. In this case, access instrumentation is initially disabled by default. It is only enabled when any instrumentation marker is found by the instrumentator and can be disabled again with a call of the _disable_access_instrumentation()_ marker. However, this directive should be used with extreme care, as it has the potential to prevent instrumentation of approximate buffer accesses, as instructions are parsed only during the first time they are executed.

9. DEFAULT_FAULT_INJECTION: Under this option, ApproxSS uses a bit-by-bit error injection method. For each bit that can be injected, a floating point number between 0.0 and 1.0 is generated. If it is below the threshold of the BER passed, based on the index of the bit in question, a mask is created for the bit inversion and the byte that will be injected is determined. Finally, the bit is flipped using bitwise logical disjunction (XOR). In terms of implementation, the random number generator used by the error injector is the default_random_engine from the standard library (std) of the C++ programming language, initially fed by a std::random_device. The pseusorandom numbers generated by it follow a uniform distribution, thanks to the std::uniform_real_distribution class.

10. GRANULAR_FAULT_INJECTION: Under this option, ApproxSS uses an element-level injection method. For every element being injected, a floating point number between 0.0 and 1.0 is generated. If it is below the threshold of the BER stacked in relation to the BitDepth, then one of the injectable bits is pseudorandomly selected and flipped. This option essencially pseudorandomly determins if one of the element’s bit should be flipped, taking into consideration their collevtive BER. Then, if that is the case, pseudorandomly selects one of the element’s bit to be flipped. This come with the restriction of only one of elements bits being able to flipped and may reduce fidelity, specially under higher BERs.
In terms of implementation, the random number generator used by the error injector is the default_random_engine from the standard library (std) of the C++ programming language, initially fed by a std::random_device. It uses two uniform distribution classes to generate pseudo random numbers, std::uniform_real_distribution, for the probability of one of the bits being injected; and std::uniform_int_distribution, to select which bit to inject.

11. DISTANCE_BASED_FAULT_INJECTOR: Under this option, ApproxSS uses a fault injection methods based on the distance between the errors. For every bit accessed, a counter for the next error is decremented. If the counter reaches zero or less, the corresponding element bit is mapped and flipped. Then, the counter is updated with a new future bit and the process repeats. In terms of implementation, the random number generator used by the error injector is the default_random_engine from the standard library (std) of the C++ programming language, initialized by a std::random_device. To pseudorandomly determine the next bit to be injected, a std::normal_distribution is used, initialized with the mean and standard deviation of distance between errors. Since the generated value can be negative, it is always converted to positive.

## Instrumentation Markers

To enable and control ApproxSS operation, some instrumentation markers must be added in the target application source code. These markers are dummy routines, which don't necessarily perform some useful function within the target application. However, thanks to their names, when they are found by Pin instrumentation, they trigger the insertion of calls to control functions over approximate buffers and error injection.

### Approximate Buffer Addition

```
void add_approx(void * const start_address,
                void const * const end_address,
                const int64_t bufferId,
                const int64_t configurationId,
                const size_t elementSize);
```

The _add_approx(. . . )_ function signals ApproxSS that an approximate buffer should be added to the list of active buffers. It has as parameters, respectively, the starting (inclusive) and the final (non-inclusive) addresses of the approximate buffer to be added, an identifier for the buffer in question, the identifier of the configuration that the buffer's error injector must follow and, finally, the size in bytes of the elements stored by this buffer.

In case the configuration identifier has not been informed in the ApproxSS inputs or the size of the elements of an approximate buffer is smaller than the length of bits of its injector configuration, an exception is generated, causing the interruption of the execution of the Pin and, consequently, of the target application. If the informed buffer crosses the range of addresses of a buffer already present in the list of active buffers, this new buffer will be ignored and will not be added, regardless of the other arguments passed. Otherwise, what if the rest of the arguments passed are identical to some buffer already present in the list of general buffers, it is added back to the list of active buffers and reactivated. If any of the other passed parameters differ, a new approximate buffer is created and added to the lists of active and general buffers.

### Approximate Buffer Removal

```
void remove_approx(void * const start_address,
                   void const * const end_address,
                   const bool giveAwayRecords = true);
```

The function _remove_approx(. . . )_ signals to ApproxSS that an approximate buffer with the same starting and ending memory addresses should be removed from the list of active and retired buffers. It has as parameters, respectively, the starting (inclusive) and the final (non-inclusive) addresses of the approximate buffer to be removed, and a flag signalizing if the injection records should be given away to a shared memory pool between approximate buffer or deallocated. The approximate buffer data is still present in the list of general buffers, to be displayed at the end of the Pin execution and possible future readmissions to the list of active buffers.
Retiring an approximate buffer implies reversing residual read errors and applying outstanding write errors. In addition, current period records are stored in buffer records.

### Period Increment

```
void next_period();
```

The _next_period()_ function increments the period marker. Periods are separations in the approximate buffer records and control the BERs in use by the error injectors (if MULTIPLE_BER_CONFIGURATION is active), allowing a more detailed view on the behavior of the target application and, possibly, also a greater control over the error injection by ApproxSS. 

The approximate buffer registers store read and write counters.
If the LOG_FAULTS flag was active during ApproxSS compilation, the log also stores bit-by-bit counters of reading, writing errors. And, in the case of the MULTIPLE_BER_CONFIGURATION flag, it also stores the indexes of the BERs, to allow the future calculation of energy consumption. By incrementing the period marker, current period log data is stored, counters are reset, and BER indexes are advanced.

The separations caused by periods can be useful to obtain individual measurements of accesses and facilitate several levels of approximation for each frame of a video, block of an image, epoch of training of meeting networks, etc. For example, in the inter-frame prediction step of video encoders, several neighboring frames are used as a reference. Different levels of approximation could be used as these frames of reference move away from the one to be predicted, if taking advantage of the reduced potential influence.

### Enabling and Disabling Error Injection

To allow a more delicate control of the error injection, ApproxSS has two variables that activate and deactivate this process, _level_ and _global_injection_enabled_. The first one is an integer and the second is a boolean. For error injection to be effectively enabled, _level_ must have a value above 0 and _global_injection_enabled_ must be true. 

The _level_'s initial state is 0 and _global_injection_enabled_'s is true. The error injection control through the _level_ is intended to make it possible for various functions of the target application to activate error injection during their executions, allowing them to call each other without the exit of any of them inadvertently disabling error injection for the rest. The control through _global_injection_enabled_ allows temporarily deactivating error injection regardless of the current value of _level_.


```
void start_level();
void end_level();
```

The _start_level()_ function increments the level counter, potentially enabling error injection. The _end_level()_ function decrements the level contactor, potentially disabling error injection. Each _start_level()_ must be accompanied by an eventual _end_level()_ so that error injection is properly disabled.

```
void enable_global_injection();
void disable_global_injection();
```

The _enable_global_injection()_ function sets the _global_injection_enabled_ to true, potentially enabling error injection. The _disable_global_injection()_ function sets the _global_injection_enabled_ to false, effectively deactivating error injection.


### Enabling and Disabling Memory Access Instrumentation

By default, due to Pin's limitations that make it impossible for it to know effective instruction addresses at instrumentation time, all memory accesses made by the application are instrumented by ApproxSS. This ends up requiring Pin to check whether accesses made to memory belong to some approximate buffer every time they are executed, causing overhead. To work around this, in an attempt to reduce the overhead caused, under the NARROW_ACCESS_INSTRUMENTATION compilation directive, the ApproxSS provides two instrumentation markers, _enable_access_instrumentation()_ and _disable_access_instrumentation()_, allowing accesses to be instrumented only at times specified by the user.

```
void enable_access_instrumentation();
void disable_access_instrumentation();
```

The _enable_access_instrumentation()_ function and the other instrumentation markers (with the exception of _disable_access_instrumentation()_) enable instrumentation of memory accesses when encountered by the instrumentator. The _disable_access_instrumentation()_ function disables the instrumentation of memory accesses when it's called.


## Usage Example

The example code below brings an example of simplified instrumentation of a generic target application, with most of the instrumentation markers available in ApproxSS. On line 1, it shows the inclusion of the library containing the instrumentation marker definitions. On line 11, an approximate buffer is added with the _add_approx()_ function, based on the starting and ending addresses of an array, buffer identifiers and the injector configuration it must follow; and the size in bytes of the stored elements. On line 12, error injection is enabled with _start_level()_. Then, for each line present in the example array, some processing is performed. The processing itself takes place on the row of the array subject to errors. However, the output of such an array line is done precisely, with the injection being temporarily disabled, regardless of the _level_ value, with 
_disable_global_injection()_ (line 17) and re-enabled with _enable_global_injection()_ (line 19) immediately after the output is done. In addition, for each line of the array, the period is incremented (line 21), allowing the individual monitoring of consumption of power and line-based BER control. Finally, injection is disabled with end_level() (line 24), and the approximate buffer is removed from the list of active buffers and retired (line 25).

```
1. #include "approx.h"
2. #include "example.h"
3. 
4. constexpr static size_t COLUMN_COUNT = 1000;
5. constexpr static size_t LINE_COUNT = 1000;
6. 
7. int main() {
8.     uint16_t example_array[LINE_COUNT][COLUMN_COUNT];
9.     uint16_t * const array_begin = (uint16_t*) &example_array;
10.    uint16_t const * const array_end = array_begin + (COLUMN_COUNT * LINE_COUNT);
11.    add_approx(array_begin, array_end, 1, 1, sizeof(uint16_t));
12.    start_level ();
13. 
14.    for (size_t i = 0; i < LINE_COUNT; ++i) {
15.        Example::ProcessLine(example_array[i], COLUMN_COUNT);
16. 
17.        disable_global_injection ();
18.        Example::OutputLine(example_array[i], COLUMN_COUNT);
19.        enable_global_injection();
20. 
21.        next_period();
22.    }
23.
24.    end_level();
25.    remove_approx(array_begin, array_end);
26. }
```

## Execution

The Pin, together with ApproxSS and the target application, are generically called as follows:

```
./[Pin executable] -t [ApproxSS] -cfg [Error Injection Configuration File] 
                                [-aof [Memory Access Log]]... 
                                [-pfl [Energy Consumption Profile]]... 
                                [-cof [Energy Consumption Log]]... 
                   -- ./[Target Application] [Target Application Options]...
```

First, the Pin's executable is called. Next, ApproxSS and the error injection configuration file are informed. A correctly formed error injection configuration file is required to start the execution. A memory access output file is optional. If one is not informed, a generically named file is created based on the execution date and time. An energy consumption profile is optional. If one is not informed, energy consumption will not be estimated. An energy consumption log is optional. If one is not informed, a generically named file is created based on the execution date and time.
Finally, the executable of the target application is called, with its options, to run on Pin alongside ApproxSS.

## Input Files
### Error Injection Configuration

As input, ApproxSS must necesseraly receive a file containing the configurations of the errors injectors that will be used on the target application. Configuration files are made up as follows, with the field name being separated from its value by a colon (:).

```
(ConfigurationId: {x ∈ int64_t}
BitDepth:   {x ∈ size_t | 0 < x}
ReadBer:    ({x ∈ double | 0.0 ≤ x < 1.0};)1|+
WriteBer:   ({x ∈ double | 0.0 ≤ x < 1.0};)1|+
PassiveBer: ({x ∈ double | 0.0 ≤ x < 1.0};)1|+
ADD_BUFFER)1|+
```

The configuration starts with the _ConfigurationId_, which serves as the configuration identifier and can be any value representable by a signed 64-bit integer (int64_t). Followed by _BitDepth_, the bit length that limits up to which bit of the accessed memory element the error injection can be performed, and can be any value above 0 representable by an unsigned 64-bit integer (size_t). However, trying to use an injector configuration with an approximate buffer whose stored indidivual element size is less than the _BitDepth_ will result in an exception being raised, interrupting the execution of both Pin and target application. _ReadBer_, _WriteBer_, and _PassiveBer_ determine the read, write, and passive BERs, respectively. They are 64-bit (double) floating point numbers, must be in the range [0,0, 1,0) and have a semicolon (;) at the end. If the MULTIPLE_BER_CONFIGURATION flag was active during the ApproxSS compilation,  _ReadBer_, _WriteBer_, and _PassiveBer_ can have several BER values. If DISTANCE_BASED_FAULT_INJECTOR is enabled, instead of the rates, BERs must be the mean and standard deviation of the distance between errors.
_PassiveBer_ is optional when ENABLE_PASSIVE_INJECTION is _false_, ending up ignored. And finally, ADD_BUFFER signals the end of the configuration in question. Several injector configurations can be specified, as long as they have different _ConfigurationIds_. If any are repeated, their configuration will be ignored and will not be added.

```
ConfigurationId: 0
BitDepth:        8
ReadBer:         10E-04;
WriteBer:        10E-05;
PassiveBer:      10E-06;
ADD_BUFFER

ConfigurationId: 1
BitDepth:        10
ReadBer:         10E-05; 10E-04;
WriteBer:        10E-06;
PassiveBer:      10E-07;
ADD_BUFFER
```

The example code above shows a sample of an error injector configuration file. In it, we can see two different configurations. The first configuration (from top to bottom), has an identifier value of 0, a _BitDepth_ of 8, limiting error injections to a maximum of the eighth bit of an element; a reading BER of 10E-04, a writing BER of 10E-05 and passive BER of 10E-06, establishing the probabilities of errors occurring in read, write and hold operations, respectively. The second configuration brings different values, with the biggest difference being the multiple BERs for reading operations. More examples are availiable under examples/injection-configurations.

### Energy Consumption Profile

As input, ApproxSS can additionally receive a file containing the energy consumption profile of approximate buffers' error injection configurations, for energy consumption estimation. Profiles are made up as follows, with the field name being separated from its value by a colon (:).

```
(ConfigurationId: {x ∈ int64_t}
NO_REFERENCE_VALUES ? (REFERENCE_VALUES
                       ReadConsumption:    ({x ∈ double | 0.0 ≤ x;)1|+
                       WriteConsumption:   ({x ∈ double | 0.0 ≤ x;)1|+
                       PassiveConsumption: ({x ∈ double | 0.0 ≤ x;)1|+)
APPROXIMATE_VALUES
ReadConsumption:    ({x ∈ double | 0.0 ≤ x;)1|+
WriteConsumption:   ({x ∈ double | 0.0 ≤ x;)1|+
PassiveConsumption: ({x ∈ double | 0.0 ≤ x;)1|+
ADD_BUFFER)1|+
```
The configuration starts with the _ConfigurationId_, which serves as the configuration identifier to designate to which error injection configuration it corresponds, and can be any value representable by a signed 64-bit integer (int64_t). 
Every energy consumption profile must have a respective error injection configuration and vice versa.
_ReadConsumption_, _WriteConsumption_, and _PassiveConsumption_ determine the read, write, and passive energy consumptions, respectively, measure in picojoules (pJ) per byte. 
They are 64-bit (double) floating point numbers, must be positive and have a semicolon (;) at the end. If the MULTIPLE_BER_CONFIGURATION flag was active during the ApproxSS compilation,_ReadConsumption_, _WriteConsumption_, and _PassiveConsumption_ can have several values and MUST be the same amount as their error injection counterparts.
_PassiveConsumption_ is optional when ENABLE_PASSIVE_INJECTION is _false_, ending up ignored. 
And finally, END_PROFILE signals the end of the profile in question.

```
ConfigurationId: 0
REFERENCE_VALUES
ReadConsumpion:     1.8025;
WriteConsumption:   1.44;
PassiveConsumption: 0;
APPROXIMATE_VALUES
ReadConsumpion:     0.7525;
WriteConsumption:   0.7475;
PassiveConsumption: 0;
END_PROFILE

ConfigurationId: 1
NO_REFERENCE_VALUES
APPROXIMATE_VALUES
ReadConsumpion:     1.1475; 0.8225;
WriteConsumption:   1.1475;
PassiveConsumption: 0;
END_PROFILE
```

The code example shows a sample of an energy consumption profile. The first profile, identified with a ConfigurationId of 0, presents consumption values for both reference voltages and approximate voltages. Reference values are preceded by the keyword “REFERENCE_VALUES”. In case there are no reference values to be declared, the appropriate keyword is “NO_REFERENCE_VALUES”, as we can see in the second profile. Consumption values for approximate voltages are preceded by “APPROXIMATE_VALUES”. The second configuration, ConfigurationId 1, does not have reference values, but brings two consumption values for write operations, referring to some error injector configuration with multiple BERs.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.