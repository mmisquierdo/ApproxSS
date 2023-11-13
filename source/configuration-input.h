#ifndef CONFIGURATION_INPUT_H
#define CONFIGURATION_INPUT_H

#include <string>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <limits>
#include <unordered_map>

#include "compiling-options.h"	
#include "injector-configuration.h"
#include "consumption-profile.h"

enum class InjectorFieldCode {
	ConfigurationId,
	BitDepth,
	ReadBer,
	WriteBer,
	PassiveBer,
	NOT_FOUND
};

const std::unordered_map<InjectorFieldCode, const std::string> InjectorFieldCodeNames = {	{InjectorFieldCode::ConfigurationId,	"ConfigurationId"},
																							{InjectorFieldCode::BitDepth,			"BitDepth"},
																							{InjectorFieldCode::ReadBer,			"ReadBer"},
																							{InjectorFieldCode::WriteBer,			"WriteBer"},
																							{InjectorFieldCode::PassiveBer,			"PassiveBer"}};


struct ConsumptionFieldCode {
	static constexpr size_t ConfigurationId		= (1 << 0);
	static constexpr size_t NO_REFERENCE_VALUES	= (1 << 1);
	static constexpr size_t REFERENCE_VALUES	= (1 << 2);
	static constexpr size_t APPROXIMATE_VALUES	= (1 << 3);
	static constexpr size_t ReadConsumption		= (1 << 4);
	static constexpr size_t WriteConsumption	= (1 << 5);
	static constexpr size_t PassiveConsumption	= (1 << 6);
	static constexpr size_t END_PROFILE 		= (1 << 7);
	static constexpr size_t NOT_FOUND 			= (1 << 8);
};

const std::unordered_map<size_t, const std::string> ConsumptionFieldCodeNames = {	{ConsumptionFieldCode::ConfigurationId,		"ConfigurationId"},
																					{ConsumptionFieldCode::NO_REFERENCE_VALUES,	"NO_REFERENCE_VALUES"},
																					{ConsumptionFieldCode::REFERENCE_VALUES,	"REFERENCE_VALUES"},
																					{ConsumptionFieldCode::APPROXIMATE_VALUES,	"APPROXIMATE_VALUES"},
																					{ConsumptionFieldCode::ReadConsumption,		"ReadConsumption"},
																					{ConsumptionFieldCode::WriteConsumption,	"WriteConsumption"},
																					{ConsumptionFieldCode::PassiveConsumption,	"PassiveConsumption"},
																					{ConsumptionFieldCode::END_PROFILE,			"END_PROFILE"}};

namespace StringHandling {
	// trim from start (in place)
	void lTrim(std::string& s);

	// trim from end (in place)
	void rTrim(std::string& s);

	// trim from both ends (in place)
	void trim(std::string& s);

	std::string trim(std::string&& s);

	void toLower(std::string& s) ;

	std::string toLower(std::string&& s);
}

namespace PintoolInput {
	size_t AssertConsumptionFieldCode(const std::string& s, const size_t expectedFieldCodes, const size_t lineCount);
	InjectorFieldCode GetInjectorFieldCode(std::string s);
	size_t GetConsumptionFieldCode(std::string s);
	size_t ErrorCategoryToConsumptionFieldCode(const size_t errorCat); //eu me odeio por essa gambiarra
	std::string GetExpectedConsumptionFieldsNames(size_t expectedFieldCodes);

	size_t CountCharacter(const std::string& values, const size_t lineCount, const char character = ';', const size_t maxSemiColonCount = std::numeric_limits<size_t>::max());

	void ProcessBerConfiguration(const std::string& values, const size_t lineCount, const size_t bitDepth, std::unique_ptr<double[]>& toAtrib);
	void ProcessBerConfiguration(const std::string& value,  const size_t lineCount, std::pair<double, double>& toAtrib);
	void ProcessBerConfiguration(const std::string& value,  const size_t lineCount, double& toAtrib);
	void ProcessBerConfiguration(const std::string& values, const size_t lineCount, InjectionConfigurationReference& injectorCfg, const size_t errorCat);

	void ProcessConsumptionValue(const std::string& value, const size_t lineCount, double& toAtrib);

	void ProcessConsumptionValue(std::ifstream& inputFile, std::string& line, size_t& lineCount, ConsumptionProfile& consumptionProfile, const InjectionConfigurationReference& respectiveInjectorCfg, const size_t consumptionType);
	void ProcessConsumptionValue(const std::string& values, const size_t lineCount, ConsumptionProfile& consumptionProfile, const InjectionConfigurationReference& respectiveInjectorCfg, const size_t consumptionType, const size_t errorCat);

	void SeparateStringOn(const std::string& inputLine, const size_t lineCount, std::string& fistPart, std::string& secondPart, const char separator);

	void ProcessInjectorConfiguration(const std::string& configurationFilename);
	void ProcessEnergyProfile(const std::string& profileFilename);

	bool GetNextValidLine(std::ifstream& inputFile, std::string& line, size_t& lineCount);
}

#endif /* CONFIGURATION_INPUT_H */