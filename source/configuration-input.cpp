#include "configuration-input.h"
#include "pin.H"

// trim from start (in place)
void StringHandling::lTrim(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {return !std::isspace(ch);}));
}

// trim from end (in place)
void StringHandling::rTrim(std::string& s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {return !std::isspace(ch);}).base(), s.end());
}

// trim from both ends (in place)
void StringHandling::trim(std::string& s) {
	StringHandling::rTrim(s);
	StringHandling::lTrim(s);
}

std::string StringHandling::trim(std::string&& s) {
	StringHandling::rTrim(s);
	StringHandling::lTrim(s);
	return s;
}

void StringHandling::toLower(std::string& s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c);});
}

std::string StringHandling::toLower(std::string&& s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c);});
	return s;
}

InjectorFieldCode PintoolInput::GetInjectorFieldCode(std::string s) {
	StringHandling::toLower(s);

	for (const auto& [code, field] : InjectorFieldCodeNames) {
		if (s == StringHandling::toLower(std::string(field))) {
			return code;
		}
	}
	
	return InjectorFieldCode::NOT_FOUND;
}

size_t PintoolInput::ErrorCategoryToConsumptionFieldCode(const size_t errorCat) {
	static constexpr size_t converterArray[ErrorCategory::Size] = {ConsumptionFieldCode::ReadConsumption, ConsumptionFieldCode::WriteConsumption
	#if ENABLE_PASSIVE_INJECTION
		, ConsumptionFieldCode::PassiveConsumption
	#endif
	};

	return converterArray[errorCat];
}

size_t PintoolInput::GetConsumptionFieldCode(std::string s) {
	StringHandling::toLower(s);

	for (const auto& [code, field] : ConsumptionFieldCodeNames) {
		if (s == StringHandling::toLower(std::string(field))) {
			return code;
		}
	}
	
	return ConsumptionFieldCode::NOT_FOUND;
}

std::string PintoolInput::GetExpectedConsumptionFieldsNames(size_t expectedFieldCodes) {
	std::string fields;

	constexpr size_t bitCountInSize_t = (sizeof(expectedFieldCodes) * BYTE_SIZE);

	while (expectedFieldCodes) {
		const size_t leadingZeroIndex = static_cast<size_t>(__builtin_clzll(expectedFieldCodes)); //0 is a undefined value for this builtin, but the loop avoids it	TODO: update to std::countl_zero when C++20 gets supported	
		const size_t filteredFieldCode = (1 << (bitCountInSize_t - leadingZeroIndex - 1));
		const std::unordered_map<size_t, const std::string>::const_iterator it = ConsumptionFieldCodeNames.find(filteredFieldCode);
		const std::string& fieldName = it->second;

		fields += ((fields.empty() ? "" : " or ") + ("\"" + fieldName + "\""));

		expectedFieldCodes = expectedFieldCodes & ~filteredFieldCode;
	}

	return fields;
}

size_t PintoolInput::AssertConsumptionFieldCode(const std::string& s, const size_t expectedFieldCodes, const size_t lineCount) {
	const size_t receivedFieldCode = GetConsumptionFieldCode(s);

	if (receivedFieldCode & expectedFieldCodes) { //yes, a bitwise AND, it's dealing with bitfields
		return receivedFieldCode;
	} else {
		std::cerr << "ApproxSS Error: malformed profile. Expected: " << GetExpectedConsumptionFieldsNames(expectedFieldCodes) << ". Found: \"" << s << "\"." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}
}

size_t PintoolInput::CountCharacter(const std::string& values, const size_t lineCount, const char character /*= ';'*/, const size_t minCharCount/*= 0*/, const size_t maxCharCount /*= std::numeric_limits<size_t>::max()*/) {
	const size_t count = static_cast<size_t>(std::count(values.begin(), values.end(), character));

	if (count < minCharCount) {
		std::cerr << "ApproxSS Error: Found less separators (\"" << character << "\") than expected. Line: " << lineCount << ". Found : " << count << ". Minimum: " << minCharCount << "." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	} 

	if (count > maxCharCount) {
		std::cerr << "ApproxSS Error: Found more separators (\"" << character << "\") than supported. Line: " << lineCount << ". Found: " << count << ". Maximum: " << maxCharCount << "." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	return count;
}

void PintoolInput::ProcessBerConfiguration(const std::string& value, const size_t lineCount, std::pair<double, double>& toAtrib) {
	std::string mean;
	std::string standardDeviaton; 

	PintoolInput::SeparateStringOn(value, lineCount, mean, standardDeviaton, ' ');

	toAtrib.first	= std::stod(mean);
	toAtrib.second	= std::stod(standardDeviaton);
}

void PintoolInput::ProcessBerConfiguration(const std::string& value, const size_t lineCount, double& toAtrib) {
	toAtrib = std::stod(value);
	if (toAtrib < 0 || toAtrib >= 1) {
		std::cerr << "ApproxSS Error: Bit Error Rates (BERs) should be present in [0.0, 1.0). Found: " << toAtrib << ". Line: " << lineCount << "." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	} 
}

void PintoolInput::ProcessConsumptionValue(const std::string& value, const size_t lineCount, double& toAtrib) {
	toAtrib = std::stod(value);
	if (toAtrib < 0) {
		std::cerr << "ApproxSS Error: energy consumption values should not be negative. Found: " << toAtrib << ". Line: " << lineCount << "." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	} 
}

void PintoolInput::ProcessBerConfiguration(const std::string& values, const size_t lineCount, const size_t bitDepth, std::unique_ptr<double[]>& toAtrib) {
	const size_t berCount = PintoolInput::CountCharacter(values, lineCount, ',') + 1; //count final ; as extra comma

	if (berCount <= 1) {
		double tempBer;
		PintoolInput::ProcessBerConfiguration(values, lineCount, tempBer);

		if (tempBer == 0) {
			return; //leaves pointer null;
		}

		toAtrib = std::make_unique<double[]>(bitDepth);
		std::fill_n(toAtrib.get(), bitDepth, tempBer);
		return;
	}

	if (berCount != bitDepth) {
		std::cerr << "ApproxSS Error: the amount of element Bit Error Rates (BERs) differs from the set bit depth (in an unsupported manner). Found: " << berCount << " instead of " << bitDepth << ". Line: " << lineCount << "." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	toAtrib = std::make_unique<double[]>(bitDepth);

	std::string nextValues = values + ','; //gambiarra
	for (size_t i = 0; i < bitDepth; ++i) {
		double tempBer;
		std::string currentValue;

		PintoolInput::SeparateStringOn(nextValues, lineCount, currentValue, nextValues, ',');
		PintoolInput::ProcessBerConfiguration(currentValue, lineCount, tempBer);

		toAtrib[i] = tempBer;
	}	
}

void PintoolInput::ProcessBerConfiguration(const std::string& values, const size_t lineCount, InjectionConfigurationReference& injectorCfg, const size_t errorCat) {
	#if MULTIPLE_BER_CONFIGURATION
		injectorCfg.SetBerCount(errorCat, PintoolInput::CountCharacter(values, lineCount, ';', 1));

		std::string nextValues = values;
		for (size_t i = 0; i < injectorCfg.GetBerCount(errorCat); ++i) {
			std::string currentValue;

			PintoolInput::SeparateStringOn(nextValues, lineCount, currentValue, nextValues, ';');

			ErrorTypeStore ber;

			#if MULTIPLE_BER_ELEMENT
				PintoolInput::ProcessBerConfiguration(currentValue, lineCount, injectorCfg.GetBitDepth(), ber);
			#else
				PintoolInput::ProcessBerConfiguration(currentValue, lineCount, ber);
			#endif

			injectorCfg.SetBer(errorCat, i, ber);
		}
	#else
		PintoolInput::CountCharacter(values, lineCount, ';', 1, 1);
		ErrorTypeStore ber;

		std::string cleanValue = values.substr(0, values.find(';'));
		
		#if MULTIPLE_BER_ELEMENT
			PintoolInput::ProcessBerConfiguration(cleanValue, lineCount, injectorCfg.GetBitDepth(), ber);
		#else
			PintoolInput::ProcessBerConfiguration(cleanValue, lineCount, ber);
		#endif

		injectorCfg.SetBer(errorCat, ber);
	#endif
}

void PintoolInput::ProcessConsumptionValue(std::ifstream& inputFile, std::string& line, size_t& lineCount, ConsumptionProfile& consumptionProfile, const InjectionConfigurationReference& respectiveInjectorCfg, const size_t consumptionType) {
	std::string field, value;
	
	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		PintoolInput::GetNextValidLine(inputFile, line, lineCount);
		PintoolInput::SeparateStringOn(line, lineCount, field, value, ':');
		PintoolInput::AssertConsumptionFieldCode(field, ErrorCategoryToConsumptionFieldCode(errorCat), lineCount);
		PintoolInput::ProcessConsumptionValue(value, lineCount, consumptionProfile, respectiveInjectorCfg, consumptionType, errorCat);
	}	

	#if !ENABLE_PASSIVE_INJECTION  //gambiarra pra permitir que profiles que tenham passiveConsumption especificada continuem podendo ser usados
		const std::streampos pos = inputFile.tellg();
		PintoolInput::GetNextValidLine(inputFile, line, lineCount);
		StringHandling::toLower(line);
		if (line.find(StringHandling::toLower("PassiveConsumption")) == std::string::npos) {
			inputFile.seekg(pos);
		}
	#endif
}

void PintoolInput::ProcessConsumptionValue(const std::string& values, const size_t lineCount, ConsumptionProfile& consumptionProfile, const InjectionConfigurationReference& respectiveInjectorCfg, const size_t consumptionType, const size_t errorCat) {
	#if MULTIPLE_BER_CONFIGURATION
		const size_t semiColonCount = PintoolInput::CountCharacter(values, lineCount, ';', 1);
		if (semiColonCount != respectiveInjectorCfg.GetBerCount(errorCat)) {
			std::cerr << "ApproxSS Error: malformed energy consumption profile. Configuration " << consumptionProfile.GetConfigurationId() << " must have the same amount of energy consumption values as BERs. Found: " << semiColonCount << " semicolons. Expected: " << respectiveInjectorCfg.GetBerCount(errorCat) << "." << std::endl; 
			PIN_ExitProcess(EXIT_FAILURE);
		}

		consumptionProfile.SetConsumptionValueCount(consumptionType, errorCat, semiColonCount);

		std::string nextValues = values;
		for (size_t i = 0; i < semiColonCount; ++i) {
			std::string currentValue;

			PintoolInput::SeparateStringOn(nextValues, lineCount, currentValue, nextValues, ';');

			double consumptionValue;
			PintoolInput::ProcessConsumptionValue(currentValue, lineCount, consumptionValue);

			consumptionProfile.SetConsumptionValue(consumptionType, errorCat, i, consumptionValue);

		}
	#else
		PintoolInput::CountCharacter(values, lineCount, ';', 1, 1);
		double consumptionValue;
		PintoolInput::ProcessConsumptionValue(values, lineCount, consumptionValue);
		consumptionProfile.SetConsumptionValue(consumptionType, errorCat, consumptionValue);
	#endif
}

void PintoolInput::SeparateStringOn(const std::string& inputLine, const size_t lineCount, std::string& fistPart, std::string& secondPart, const char separator) {
	const size_t pos = inputLine.find(separator);
	if (pos == std::string::npos) {
		std::cerr << "ApproxSS Error: malformed configuration. Missing: \"" << separator << "\". Line: " << lineCount << ". Found: \"" << inputLine <<  "\"." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	fistPart 	= StringHandling::trim(inputLine.substr(0, pos));
	secondPart 	= StringHandling::trim(inputLine.substr(pos+1));
}

bool PintoolInput::GetNextValidLine(std::ifstream& inputFile, std::string& line, size_t& lineCount) {
	bool readSuccess;
	while ((readSuccess = !std::getline(inputFile, line).fail())) {
		lineCount++;

		StringHandling::trim(line);

		if (!(line.size() <= 1 || line[0] == '#')) {
			break;
		}
	}
	return readSuccess;
}

void PintoolInput::ProcessInjectorConfiguration(const std::string& configurationFilename) { //TODO: tornar configurações normais e de distancia incompativeis entre si
	std::ifstream inputFile(configurationFilename);

	if (!inputFile) {
		std::cerr << "ApproxSS Error: Unable to open injector configuration file." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	std::string line;
	size_t lineCount = 0;

	InjectionConfigurationReference* injectorCfg = new InjectionConfigurationReference();

	while (PintoolInput::GetNextValidLine(inputFile, line, lineCount)) {

		if (line.find("ADD_BUFFER") != std::string::npos) {
			const InjectorConfigurationMap::const_iterator lb = g_injectorConfigurations.lower_bound(injectorCfg->GetConfigurationId());

			if (lb == g_injectorConfigurations.cend() || (g_injectorConfigurations.key_comp()(injectorCfg->GetConfigurationId(), lb->first))) {
				g_injectorConfigurations.emplace_hint(lb, injectorCfg->GetConfigurationId(), std::unique_ptr<InjectionConfigurationReference>(injectorCfg));
			} else {
				std::cout << "Warning: ConfigurationId already specified. Discarding and ignoring it. Line " << lineCount << std::endl;
				delete injectorCfg;
			}

			injectorCfg = new InjectionConfigurationReference();
			continue;
		}

		std::string field, value;
		PintoolInput::SeparateStringOn(line, lineCount, field, value, ':');

		switch (GetInjectorFieldCode(field)) {
			case InjectorFieldCode::ConfigurationId:
				{
					const int64_t configurationIdValue = std::stoll(value);				
					injectorCfg->SetConfigurationId(configurationIdValue);
				}
				break;
			case InjectorFieldCode::BitDepth:
				{
					const int64_t bitDepthValue = std::stoll(value);
					if (bitDepthValue <= 0) {
						std::cerr << "ApproxSS Error: Bit depth must be over 0 bits. Line: \"" << lineCount << "\". Found: " << bitDepthValue << "." << std::endl;
						PIN_ExitProcess(EXIT_FAILURE);
					}
					injectorCfg->SetBitDepth(static_cast<size_t>(bitDepthValue));
				}
				break;
			case InjectorFieldCode::ReadBer:
				PintoolInput::ProcessBerConfiguration(value, lineCount, *injectorCfg, ErrorCategory::Read);
				break;
			case InjectorFieldCode::WriteBer:
				PintoolInput::ProcessBerConfiguration(value, lineCount, *injectorCfg, ErrorCategory::Write);
				break;
			case InjectorFieldCode::PassiveBer:
				#if ENABLE_PASSIVE_INJECTION
					PintoolInput::ProcessBerConfiguration(value, lineCount, *injectorCfg, ErrorCategory::Passive);
				#else
					std::cout << "ApproxSS warning: Passive injection configuration detected, but not supported." << std::endl;
				#endif
				break;
			default:
				std::cerr << ("ApproxSS Error: malformed configuration. Unrecognized field: \"" + field + "\". Line: " + std::to_string(lineCount) + ".") << std::endl;
				PIN_ExitProcess(EXIT_FAILURE);
				break;
		}
	}

	inputFile.close();

	std::cout << std::string(50, '#') << std::endl;
	std::cout << "BUFFER CONFIGURATIONS:" << std::endl;
	size_t cfgIndex = 0;
	for (const auto& [_, injectorCfg] : g_injectorConfigurations) {
		std::cout << injectorCfg->toString("\t");

		if (cfgIndex != (g_injectorConfigurations.size() - 1)) {
			std::cout << std::endl;
		}
		++cfgIndex;
	}
	std::cout << std::string(50, '#') << std::endl;
}

void PintoolInput::ProcessEnergyProfile(const std::string& profileFilename) {
	if (profileFilename.empty()) {
		std::cout << "ApproxSS reminder: memory energy consumption profile not informed. Energy consumption will not be estimated." << std::endl;	
		return;
	}

	std::ifstream inputFile(profileFilename);

	if (!inputFile) {
		std::cerr << "ApproxSS Error: Unable to open memory energy consumption profile." << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	std::string line;
	size_t lineCount = 0;

	while (PintoolInput::GetNextValidLine(inputFile, line, lineCount)) {
		ConsumptionProfile* consumptionProfile = nullptr;
		std::string field, value;

		PintoolInput::SeparateStringOn(line, lineCount, field, value, ':');
		PintoolInput::AssertConsumptionFieldCode(field, ConsumptionFieldCode::ConfigurationId, lineCount);
		const int64_t configurationId = std::stoll(value);
		consumptionProfile = new ConsumptionProfile(configurationId);

		const InjectorConfigurationMap::const_iterator injectorIt = g_injectorConfigurations.find(configurationId);
		if (injectorIt == g_injectorConfigurations.cend()) {
			std::cerr << "ApproxSS Error: respective injector configuration not specified. Found id: " << configurationId << "." << std::endl;
			PIN_ExitProcess(EXIT_FAILURE);
		}
		const InjectionConfigurationReference& respectiveInjectorCfg = *(injectorIt->second);

		PintoolInput::GetNextValidLine(inputFile, line, lineCount);
		const size_t readFieldCode = PintoolInput::AssertConsumptionFieldCode(line, ConsumptionFieldCode::NO_REFERENCE_VALUES | ConsumptionFieldCode::REFERENCE_VALUES, lineCount);

		if (ConsumptionFieldCode::REFERENCE_VALUES == readFieldCode) {
			consumptionProfile->SetHasReference(true);
			PintoolInput::ProcessConsumptionValue(inputFile, line, lineCount, *consumptionProfile, respectiveInjectorCfg, ConsumptionType::Reference);
		} else {
			consumptionProfile->SetHasReference(false);
		}

		PintoolInput::GetNextValidLine(inputFile, line, lineCount);
		PintoolInput::AssertConsumptionFieldCode(line, ConsumptionFieldCode::APPROXIMATE_VALUES, lineCount);
		PintoolInput::ProcessConsumptionValue(inputFile, line, lineCount, *consumptionProfile, respectiveInjectorCfg, ConsumptionType::Approximate);

		PintoolInput::GetNextValidLine(inputFile, line, lineCount);
		PintoolInput::AssertConsumptionFieldCode(line, ConsumptionFieldCode::END_PROFILE, lineCount);

		g_consumptionProfiles.emplace(consumptionProfile->GetConfigurationId(), std::unique_ptr<ConsumptionProfile>(consumptionProfile));
	}

	inputFile.close();

	for (const auto& [configurationId, _] : g_injectorConfigurations) {
		if (g_consumptionProfiles.find(configurationId) == g_consumptionProfiles.cend()) {
			std::cerr << "ApproxSS Error: injector configuration " << configurationId << " does not have a corresponding energy consumption profile." << std::endl;
			PIN_ExitProcess(EXIT_FAILURE);
		}
	}

	std::cout << std::string(50, '#') << std::endl;
	std::cout << "ENERGY CONFIGURATION PROFILES:" << std::endl;
	size_t cfgIndex = 0;
	for (const auto& [_, consumptionProfile] : g_consumptionProfiles) {
		std::cout << consumptionProfile->toString("\t");

		if (cfgIndex != (g_consumptionProfiles.size() - 1)) {
			std::cout << std::endl;
		}

		++cfgIndex;
	}
	std::cout << std::string(50, '#') << std::endl;
}
