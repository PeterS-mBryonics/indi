/*******************************************************************************
  Copyright(c) 2021 Chrysikos Efstathios. All rights reserved.

  Pegasus NYX

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include "lx200_pegasus_nyx101.h"
#include "lx200driver.h"
#include "indicom.h"
#include "connectionplugins/connectionserial.h"
#include <libnova/transform.h>

#include <cmath>
#include <cstring>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <regex>

#include <iostream>
using namespace std;
#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

const char *SETTINGS_TAB  = "Settings";
const char *STATUS_TAB = "Status";

// Different error codes
const char  *stationary = u8"Stationary";
const char  *moving = u8"Moving";
const char  *ok = u8"OK";
const char  *fault = u8"FAULT";

const char *LX200NYX101::getDefaultName()
{
    return "Pegasus NYX-101";
}

const char *ON = u8"ON";
const char *OFF = u8"OFF";


LX200NYX101::LX200NYX101()
{
    setVersion(1, 0);

    setLX200Capability(LX200_HAS_PULSE_GUIDING);

    SetTelescopeCapability(TELESCOPE_CAN_PARK |
                           TELESCOPE_CAN_SYNC |
                           TELESCOPE_CAN_GOTO |
                           TELESCOPE_CAN_ABORT |
                           TELESCOPE_CAN_CONTROL_TRACK |
                           TELESCOPE_HAS_TIME |
                           TELESCOPE_HAS_LOCATION |
                           TELESCOPE_HAS_TRACK_MODE,
                                 SLEW_MODES);
}

bool LX200NYX101::initProperties()
{
    LX200Generic::initProperties();

    SetParkDataType(PARK_NONE);
    timeFormat = LX200_24;
    serialConnection->setDefaultBaudRate(Connection::Serial::B_115200);

    // Mount Type
    int mountType = Equatorial;
    IUGetConfigOnSwitchIndex(getDeviceName(), "MOUNT_TYPE", &mountType);
    MountTypeSP[AltAz].fill("AltAz", "AltAz", mountType == AltAz ? ISS_ON : ISS_OFF);
    MountTypeSP[Equatorial].fill("Equatorial", "Equatorial", mountType == Equatorial ? ISS_ON : ISS_OFF);
    MountTypeSP.fill(getDeviceName(), "MOUNT_TYPE", "Mount Type", SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    if (mountType == Equatorial)
        SetTelescopeCapability(GetTelescopeCapability() | TELESCOPE_HAS_PIER_SIDE, SLEW_MODES);

    // Overwrite TRACK_CUSTOM, with TRACK_KING
    IUFillSwitch(&TrackModeS[TRACK_KING], "TRACK_KING", "King", ISS_OFF);
	
	// Horizontal Coordinates
	AltAzNP[AZ].fill("AZ", "AZ (dd:mm:ss)", "%010.6m", 0, 360, 0, 0);
	AltAzNP[ALT].fill("ALT", "ALT (dd:mm:ss)", "%010.6m", -90, 90, 0, 0);
	AltAzNP.fill(getDeviceName(), "HORIZONTAL_COORD", "ALT/AZ", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);
   
    // Elevation Limits
    ElevationLimitNP[OVERHEAD].fill("ELEVATION_OVERHEAD", "Overhead", "%g", 60, 90,   1, 90);
    ElevationLimitNP[HORIZON].fill("ELEVATION_HORIZON", "Horizon", "%g", -30, 0,   1, 0);
    ElevationLimitNP.fill(getDeviceName(), "ELEVATION_LIMIT", "Elevation Limit", SITE_TAB, IP_RW, 0,
                          IPS_IDLE);
	

    // Meridian
    MeridianLimitNP[0].fill("VALUE", "Degrees (+/- 120)", "%.f", -120, 120, 1, 0);
    MeridianLimitNP.fill(getDeviceName(), "MERIDIAN_LIMIT", "Limit", SITE_TAB, IP_RW, 60, IPS_IDLE);

    // Flip 
    FlipSP[0].fill("Flip", "Flip", ISS_OFF);
    FlipSP.fill(getDeviceName(), "FLIP", "Pier Side", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Refraction
    RefractSP[REFRACT_ON].fill("REFRACTION_ON", "On", ISS_OFF);
    RefractSP[REFRACT_OFF].fill("REFRACTION_OFF", "Off", ISS_OFF);
    RefractSP.fill(getDeviceName(), "REFRACTION",
                   "Refraction", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Safety Limits
    SafetyLimitSP[SET_SAFETY_LIMIT].fill("SET_SAFETY_LIMIT", "Set", ISS_OFF);
    SafetyLimitSP[CLEAR_SAFETY_LIMIT].fill("CLEAR_SAFETY_LIMIT", "Clear", ISS_OFF);
    SafetyLimitSP.fill(getDeviceName(), "SAFETY_LIMIT",
                       "Custom Limits", SITE_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Guide Rate
    int guideRate = 1;
    IUGetConfigOnSwitchIndex(getDeviceName(), "GUIDE_RATE", &guideRate);
    GuideRateSP[0].fill("0.25","0.25", guideRate == 0 ? ISS_ON : ISS_OFF);
    GuideRateSP[1].fill("0.50","0.50", guideRate == 1 ? ISS_ON : ISS_OFF);
    GuideRateSP[2].fill("1.00","1.00", guideRate == 2 ? ISS_ON : ISS_OFF);
    GuideRateSP.fill(getDeviceName(), "GUIDE_RATE", "Guide Rate", SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    //Go Home
    HomeSP[0].fill("Home", "Go", ISS_OFF);
    HomeSP.fill(getDeviceName(), "HOME_GO", "Home go", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    //Reset Home
    ResetHomeSP[0].fill("Home", "Reset", ISS_OFF);
    ResetHomeSP.fill(getDeviceName(), "HOME_RESET", "Home Reset", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
	
	//Set Park Position
    SetParkSP[0].fill("Park", "Set", ISS_OFF);
    SetParkSP.fill(getDeviceName(), "PARK_SET", "Set Park Pos.", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);


    // Report and debug - Removed but left here if needed for future development


#ifdef DEBUG_NYX    
    DebugCommandTP[0].fill("Command", "", "");
    DebugCommandTP.fill(getDeviceName(), "DebugCommand", "", MAIN_CONTROL_TAB, IP_RW, 0,
                        IPS_IDLE);
	Report[0].fill("Report","GU",ok);
    Report.fill(getDeviceName(), "Report", "Report", STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsTracking[0].fill("IsTracking","n",OFF);
    IsTracking.fill(getDeviceName(),"IsTracking","IsTracking",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsSlewCompleted[0].fill("IsSlewCompleted","N",OFF);
    IsSlewCompleted.fill(getDeviceName(),"IsSlewCompleted","IsSlewCompleted",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsParked[0].fill("IsParked","p/P",OFF);
    IsParked.fill(getDeviceName(),"IsParked","IsParked",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsParkginInProgress[0].fill("IsParkginInProgress","I",OFF);
    IsParkginInProgress.fill(getDeviceName(),"IsParkginInProgress","IsParkginInProgress",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsAtHomePosition[0].fill("IsAtHomePosition","H",OFF);
    IsAtHomePosition.fill(getDeviceName(),"IsAtHomePosition","IsAtHomePosition",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    MountAltAz[0].fill("MountAltAz","A",OFF);
    MountAltAz.fill(getDeviceName(),"MountAltAz","MountAltAz",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    MountEquatorial[0].fill("MountEquatorial","E",OFF);
    MountEquatorial.fill(getDeviceName(),"MountEquatorial","MountEquatorial",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    PierNone[0].fill("PierNone","",OFF);
    PierNone.fill(getDeviceName(),"PierNone","PierNone",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    PierEast[0].fill("PierEast","T",OFF);
    PierEast.fill(getDeviceName(),"PierEast","PierEast",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    PierWest[0].fill("PierWest","W",OFF);
    PierWest.fill(getDeviceName(),"PierWest","PierWest",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    DoesRefractionComp[0].fill("DoesRefractionComp","r",OFF);
    DoesRefractionComp.fill(getDeviceName(),"DoesRefractionComp","DoesRefractionComp",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    WaitingAtHome[0].fill("WaitingAtHome","w",OFF);
    WaitingAtHome.fill(getDeviceName(),"WaitingAtHome","WaitingAtHome",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    IsHomePaused[0].fill("IsHomePaused","u",OFF);
    IsHomePaused.fill(getDeviceName(),"IsHomePaused","IsHomePaused",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    ParkFailed[0].fill("ParkFailed","F",OFF);
    ParkFailed.fill(getDeviceName(),"ParkFailed","ParkFailed",STATUS_TAB, IP_RO, 60, IPS_IDLE);

    SlewingHome[0].fill("SlewingHome","h",OFF);
    SlewingHome.fill(getDeviceName(),"SlewingHome","SlewingHome",STATUS_TAB, IP_RO, 60, IPS_IDLE);
	// End of report and debug
#endif
	
    // Reboot 
    RebootSP[0].fill("Reboot", "Reboot", ISS_OFF);
    RebootSP.fill(getDeviceName(), "REBOOT", "Reboot", SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);


    // Slew Rates
    strncpy(SlewRateS[0].label, "2x", MAXINDILABEL);
    strncpy(SlewRateS[1].label, "8x", MAXINDILABEL);
    strncpy(SlewRateS[2].label, "16x", MAXINDILABEL);
    strncpy(SlewRateS[3].label, "64x", MAXINDILABEL);
    strncpy(SlewRateS[4].label, "128x", MAXINDILABEL);
    strncpy(SlewRateS[5].label, "200x", MAXINDILABEL);
    strncpy(SlewRateS[6].label, "300x", MAXINDILABEL);
    strncpy(SlewRateS[7].label, "600x", MAXINDILABEL);
    strncpy(SlewRateS[8].label, "900x", MAXINDILABEL);
    strncpy(SlewRateS[9].label, "1200x", MAXINDILABEL);
    IUResetSwitch(&SlewRateSP);

    SlewRateS[9].s = ISS_ON;
    
    // Slew rate controls per axis for satellite tracking
    RateNP[RA].fill(
        "RA_SLEW_RATE",
        "RA Slew Rate",
        "%g",
        -5., 5., 0.1, 5.);
    
    RateNP[DEC].fill(
        "DEC_SLEW_RATE",
        "DEC Slew Rate",
        "%g",
        -5., 5., 0.1, 5.);
    RateNP.fill(
        getDeviceName(),
        "SLEW_RATES",
        "Slew Rates",
        MOTION_TAB,
        IP_RW,
        60, IPS_IDLE);
    
	// Spiral search
	SpiralSP[START].fill("SpiralSearchStart", "Start", ISS_OFF);
	SpiralSP[STOP].fill("SpiralSearchStop", "Stop", ISS_OFF);
	SpiralSP.fill(getDeviceName(), "SpiralSearch", "Spiral Search at current guide rate", MOTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    
    // The hard limit switch
    RAHardLimitTP[0].fill("RAHardLimit", "n", "-");
    RAHardLimitTP.fill(getDeviceName(), "RAHardLimit", "Hard limit state", STATUS_TAB, IP_RO, 60, IPS_IDLE);
    // RA motor status
    RAMotorStatusTP[0].fill("RAMotorStatus", "n", ok);
    RAMotorStatusTP.fill(getDeviceName(), "RAMotorStatus", "RA Motor Status", STATUS_TAB, IP_RO, 60, IPS_IDLE);
    // DEC motor status
    DECMotorStatusTP[0].fill("DECMotorStatus", "n", ok);
    DECMotorStatusTP.fill(getDeviceName(), "DECMotorStatus", "DEC Motor Status", STATUS_TAB, IP_RO, 60, IPS_IDLE);
        


    return true;
}

bool LX200NYX101::updateProperties()
{
    LX200Generic::updateProperties();

    if (isConnected())
    {
        char status[DRIVER_LEN] = {0};
        if (sendCommand(":GU#", status))
        {
            if(strchr(status,'P'))
                SetParked(true);
            else
                SetParked(false);

            MountTypeSP.reset();
            MountTypeSP[AltAz].setState(strchr(status, 'A') ? ISS_ON : ISS_OFF);
            MountTypeSP[Equatorial].setState(strchr(status, 'E') ? ISS_ON : ISS_OFF);
            MountTypeSP.setState(IPS_OK);
            MountTypeSP.apply();
        }

        if(sendCommand(":GX90#", status))
        {
            std::string c = status;

            GuideRateSP.reset();
            GuideRateSP[0].setState((c.find("0.25") != std::string::npos) ? ISS_ON : ISS_OFF);
            GuideRateSP[1].setState((c.find("0.50") != std::string::npos) ? ISS_ON : ISS_OFF);
            GuideRateSP[2].setState((c.find("1.00") != std::string::npos) ? ISS_ON : ISS_OFF);
            GuideRateSP.setState((IPS_OK));
            GuideRateSP.apply();
        }
        
        if(sendCommand(":Go#", status))
        {
            std::string c = status;
            ElevationLimitNP[OVERHEAD].value = std::stoi(c);
        }

        if(sendCommand(":Gh#", status))
        {
            std::string c = status;
            ElevationLimitNP[HORIZON].value = std::stoi(c);
        }

        if(sendCommand(":GXE9#", status))
        {
            std::string c = status;
            MeridianLimitNP[0].setValue(std::stoi(c));
        }

		defineProperty(AltAzNP);
        defineProperty(MountTypeSP);
        defineProperty(GuideRateSP);
        defineProperty(HomeSP);
        defineProperty(ResetHomeSP);
		defineProperty(SetParkSP);
        defineProperty(FlipSP);
        defineProperty(MeridianLimitNP);
        defineProperty(ElevationLimitNP);
        defineProperty(RefractSP);
        defineProperty(SafetyLimitSP);
#ifdef DEBUG_NYX
        defineProperty(DebugCommandTP);
		defineProperty(Report);
		defineProperty(IsTracking);
        defineProperty(IsSlewCompleted);
        defineProperty(IsParked);
        defineProperty(IsParkginInProgress);
        defineProperty(IsAtHomePosition);
        defineProperty(MountAltAz);
        defineProperty(MountEquatorial);
        defineProperty(PierNone);
        defineProperty(PierEast);
        defineProperty(PierWest);
        defineProperty(DoesRefractionComp);
        defineProperty(WaitingAtHome);
        defineProperty(IsHomePaused);
        defineProperty(ParkFailed);
        defineProperty(SlewingHome);
#endif
        defineProperty(RebootSP);
        defineProperty(RateNP);
        defineProperty(RAHardLimitTP);
        defineProperty(RAMotorStatusTP);
        defineProperty(DECMotorStatusTP);
		defineProperty(SpiralSP);
		
    }
    else
    {
		deleteProperty(AltAzNP);
        deleteProperty(MountTypeSP);
        deleteProperty(GuideRateSP);
        deleteProperty(HomeSP);
        deleteProperty(MeridianLimitNP);
        deleteProperty(FlipSP);
        deleteProperty(ElevationLimitNP);
        deleteProperty(SafetyLimitSP);
        deleteProperty(ResetHomeSP);
		deleteProperty(SetParkSP);

#ifdef DEBUG_NYX
        deleteProperty(DebugCommandTP);
		deleteProperty(Report);
        deleteProperty(IsTracking);
        deleteProperty(IsSlewCompleted);
        deleteProperty(IsParked);
        deleteProperty(IsParkginInProgress);
        deleteProperty(IsAtHomePosition);
        deleteProperty(MountAltAz);
        deleteProperty(MountEquatorial);
        deleteProperty(PierNone);
        deleteProperty(PierEast);
        deleteProperty(PierWest);
        deleteProperty(DoesRefractionComp);
        deleteProperty(WaitingAtHome);
        deleteProperty(IsHomePaused);
        deleteProperty(ParkFailed);
        deleteProperty(SlewingHome);
#endif
        deleteProperty(RebootSP);
        deleteProperty(RefractSP);

        deleteProperty(RateNP);
        deleteProperty(RAHardLimitTP);
        deleteProperty(RAMotorStatusTP);
        deleteProperty(DECMotorStatusTP);
		deleteProperty(SpiralSP);

    }

    return true;
}

void LX200NYX101::SetPropertyText(INDI::PropertyText propertyTxt, IPState state)
{

    if(state == IPS_OK)
    {
        propertyTxt[0].setText(ON);
    }
    else if(state == IPS_BUSY)
    {
        propertyTxt[0].setText(OFF);
    }
    else if(state == IPS_IDLE)
    {
        propertyTxt[0].setText("-");
    }
    propertyTxt.setState(state);
    propertyTxt.apply();
}

bool LX200NYX101::ReadScopeStatus()
{
    if (!isConnected())
        return false;
	bool _IsTracking = true;
	bool _IsSlewCompleted = false;
	bool _IsParked = false;
	bool _DoesRefractionComp = false;
	TelescopePierSide _PierSide = PIER_UNKNOWN;
	NYXTelescopeTrackMode _TrackingMode = TRACK_SIDEREAL;
#ifdef DEBUG_NYX
    
    SetPropertyText(IsTracking, IPS_OK);
    SetPropertyText(IsSlewCompleted, IPS_BUSY);
    SetPropertyText(IsParked, IPS_BUSY);
    bool _IsParkginInProgress = false;
    SetPropertyText(IsParkginInProgress, IPS_BUSY);
    bool _IsAtHomePosition = false;
    SetPropertyText(IsAtHomePosition, IPS_BUSY);
    MountType _MountType = Equatorial;
    SetPropertyText(DoesRefractionComp, IPS_BUSY);
    bool _WaitingAtHome = false;
    SetPropertyText(WaitingAtHome, IPS_BUSY);
    bool _IsHomePaused = false;
    SetPropertyText(IsHomePaused, IPS_BUSY);
    bool _ParkFailed = false;
    SetPropertyText(ParkFailed, IPS_BUSY);
    bool _SlewingHome = false;
    SetPropertyText(SlewingHome, IPS_BUSY);
#endif
	// Getting the status message from the mount and interpreting it
	char status[DRIVER_LEN] = {0};
    
    if(sendCommand(":GU#", status))
    {
        int index = 0;
        while(true)
        {
            switch (status[index++])
            {
            case 'n':
                _IsTracking = false;
				#ifdef DEBUG_NYX
                SetPropertyText(IsTracking, IPS_BUSY);
				#endif
                continue;
            case 'N':
                _IsSlewCompleted = true;
				#ifdef DEBUG_NYX
                SetPropertyText(IsSlewCompleted, IPS_OK);
				#endif
                continue;
            case 'p':
                _IsParked = false;
				#ifdef DEBUG_NYX
                SetPropertyText(IsParked, IPS_BUSY);
				#endif
                continue;
            case 'P':
                _IsParked = true;
				#ifdef DEBUG_NYX
                SetPropertyText(IsParked, IPS_OK);
				#endif
                continue;
            case 'I':
				#ifdef DEBUG_NYX
                _IsParkginInProgress = true;
                SetPropertyText(IsParkginInProgress, IPS_OK);
				#endif
                continue;
            case 'H':
				#ifdef DEBUG_NYX
                _IsAtHomePosition = true;
                SetPropertyText(IsAtHomePosition, IPS_OK);
				#endif
                continue;
            case '(':
                _TrackingMode = TRACK_LUNAR;
                continue;
            case 'O':
                _TrackingMode = TRACK_SOLAR;
                continue;
            case 'k':
                _TrackingMode = TRACK_KING;
                continue;
            case 'A':
				#ifdef DEBUG_NYX
                _MountType = AltAz;
                SetPropertyText(MountAltAz, IPS_OK);
                SetPropertyText(MountEquatorial, IPS_BUSY);
				#endif
                continue;
            case 'E':
				#ifdef DEBUG_NYX
                _MountType = Equatorial;
                SetPropertyText(MountEquatorial, IPS_OK);
                SetPropertyText(MountAltAz, IPS_BUSY);
				#endif
                continue;
            case 'T':
                _PierSide = PIER_EAST;
                continue;
            case 'W':
                _PierSide = PIER_WEST;
                continue;
            case 'r':
                _DoesRefractionComp = true;
				#ifdef DEBUG_NYX
                SetPropertyText(DoesRefractionComp, IPS_OK);
				#endif
                continue;
            case 'w':
				#ifdef DEBUG_NYX
                _WaitingAtHome = true;
                SetPropertyText(WaitingAtHome, IPS_OK);
				#endif
                continue;
            case 'u':
				#ifdef DEBUG_NYX
                _IsHomePaused = true;
                SetPropertyText(IsHomePaused, IPS_OK);
				#endif
                continue;
            case 'F':
				#ifdef DEBUG_NYX
                _ParkFailed = true;
                SetPropertyText(ParkFailed, IPS_OK);
				#endif
                continue;
            case 'h':
				#ifdef DEBUG_NYX
                _SlewingHome = true;
                SetPropertyText(SlewingHome, IPS_OK);
				#endif
                continue;
            case '#':
                break;
            default:
                continue;
            }
            break;
        }
    }
	else
	{

	}
	
	
    if(_DoesRefractionComp ){
        RefractSP[REFRACT_ON].setState(ISS_ON);
        RefractSP[REFRACT_OFF].setState(ISS_OFF);
        RefractSP.setState(IPS_OK);
        RefractSP.apply();
     } else {
        RefractSP[REFRACT_ON].setState(ISS_OFF);
        RefractSP[REFRACT_OFF].setState(ISS_ON);
        RefractSP.setState(IPS_OK);
        RefractSP.apply();
    }
    TrackModeS[TRACK_SIDEREAL].s = ISS_OFF;
    TrackModeS[TRACK_LUNAR].s = ISS_OFF;
    TrackModeS[TRACK_SOLAR].s = ISS_OFF;
    TrackModeS[TRACK_KING].s = ISS_OFF;
    TrackModeS[_TrackingMode].s = ISS_ON;
    TrackModeSP.s   = IPS_OK;
    IDSetSwitch(&TrackModeSP, nullptr);
#ifdef DEBUG_NYX
    switch(_PierSide)
    {
        case INDI::Telescope::PIER_UNKNOWN:
            SetPropertyText(PierNone, IPS_OK);
            SetPropertyText(PierEast, IPS_BUSY);
            SetPropertyText(PierWest, IPS_BUSY);
            break;
        case INDI::Telescope::PIER_EAST:
            SetPropertyText(PierEast, IPS_OK);
            SetPropertyText(PierNone, IPS_BUSY);
            SetPropertyText(PierWest, IPS_BUSY);
            break;
        case INDI::Telescope::PIER_WEST:
            SetPropertyText(PierWest, IPS_OK);
            SetPropertyText(PierEast, IPS_BUSY);
            SetPropertyText(PierNone, IPS_BUSY);
            break;
    }
#endif

    if (TrackState == SCOPE_SLEWING)
    {
        if (_IsSlewCompleted)
        {
            TrackState = SCOPE_TRACKING;
            LOG_INFO("Slew is complete. Tracking...");
        }
    }
    else if (TrackState != SCOPE_PARKED && _IsParked)
    {
        SetParked(true);
    }
    else
    {
        auto wasTracking = TrackStateS[INDI_ENABLED].s == ISS_ON;
        if (wasTracking != _IsTracking)
            TrackState = _IsTracking ? SCOPE_TRACKING : SCOPE_IDLE;
    }


    if (getLX200RA(PortFD, &currentRA) < 0 || getLX200DEC(PortFD, &currentDEC) < 0)
    {
        EqNP.s = IPS_ALERT;
        IDSetNumber(&EqNP, "Error reading Ra - Dec");
        return false;
    }

    if (HasPierSide())
    {
        char response[DRIVER_LEN] = {0};
        if (sendCommand(":Gm#", response))
        {
            if (response[0] == 'W')
                setPierSide(PIER_WEST);
            else if (response[0] == 'E')
                setPierSide(PIER_EAST);
            else
                setPierSide(PIER_UNKNOWN);
        }
    }

    NewRaDec(currentRA, currentDEC);

    
	// RA Motor Status
    char RaStatus[DRIVER_LEN] = {0};
    if(sendCommand(":GXU1#", RaStatus))
    {
		char s[DRIVER_LEN] = {0};
		GetMotorState(s, RaStatus);
		RAMotorStatusTP[0].setText(s);
    }
    RAMotorStatusTP.apply();

	// DEC Motor Status
    char DecStatus[DRIVER_LEN] = {0};
    if(sendCommand(":GXU2#", DecStatus))
    {
        char s[DRIVER_LEN] = {0};
		GetMotorState(s, DecStatus);
		DECMotorStatusTP[0].setText(s);
    }
    DECMotorStatusTP.apply();
	
	// RA limit switch status
	
    char RaLimitStatus[DRIVER_LEN] = {0};
    sendCommand(":GX9L#", RaLimitStatus);
    if(RaLimitStatus[0] == '1')
    {
		SetPropertyText(RAHardLimitTP, IPS_OK);
    }
    else
    {
		SetPropertyText(RAHardLimitTP, IPS_IDLE);
    }
    RAHardLimitTP.apply();
    
	// Alt/Az Position Read
	if (getLX200Az(PortFD, &currentAz) < 0 || getLX200Alt(PortFD, &currentAlt) < 0)
	{
		AltAzNP.setState(IPS_ALERT);
        return false;
	}
	else
	{
		AltAzNP[AZ].value = currentAz;
		AltAzNP[ALT].value = currentAlt;
	}
	AltAzNP.apply();
	
	// Minutes past meridian read
	if (getFloat(PortFD, &minPastEastMeridian, ":GXE9#") == 0 && getFloat(PortFD, &minPastWestMeridian, ":GXEA#") == 0)
	{
		
		if (minPastEastMeridian != minPastWestMeridian)
		{
			// If the two values aren't equal, set them to the same number
			std::string command = ":SXE9," + std::to_string(MeridianLimitNP[0].getValue()) + "#";;
            sendCommand(command.c_str());
            command = ":SXEA," + std::to_string(MeridianLimitNP[0].getValue()) + "#";;
            sendCommand(command.c_str());
		}
		else
		{
			MeridianLimitNP[0].value = minPastEastMeridian;
			MeridianLimitNP.setState(IPS_OK);
		}
	}
	else
	{
		MeridianLimitNP.setState(IPS_ALERT);
	}
	MeridianLimitNP.apply();
	
    return true;
}

bool LX200NYX101::GetMotorState(char * s, char * status)
{
	// Gets the motor state according to the table in the command set
	// Comes out of the mount as a comma separated list
	// TODO: add parsing of the motor load number at the end
	// Currently if there's a fault condition it just says "FAULT"
	// as I'm not sure if the extra information is that useful
	string segment;
	vector<string> seglist;
	stringstream StatusString(status);
	while(getline(StatusString, segment, ','))
	{
	   seglist.push_back(segment);
	}
	// Unknown state - probably a fault if none of these trigger
	strcpy(s, fault);
	if (seglist[0] == "ST")
		{
		// Motor stationary
		strcpy(s, stationary);
		}
	else if (seglist[0] != "ST")
		{
		// Motor slewing normally
		strcpy(s, moving);
		}
	// We want to override the stationary/moving in the case of any fault
	if (seglist[1] == "OA")
		{
		// Output A open load
		strcpy(s, fault);
		}
	else if (seglist[2] == "OB")
		{
		// Output B open load
		strcpy(s, fault);
		}
	else if (seglist[3] == "GA")
		{
		// Output A short to ground
		strcpy(s, fault);
		}
	else if (seglist[4] == "GB")
		{
		// Output B short to ground
		strcpy(s, fault);
		}
	else if (seglist[5] == "OT")
		{
		// Temp > 150
		strcpy(s, fault);
		}
	else if (seglist[6] == "PW")
		{
		// Temp > 120 C
		strcpy(s, fault);
		}
	else if (seglist[7] == "GF")
		{
		// motor fault
		strcpy(s, fault);
		}
	return true;
}


// Start and stop spiral
// ideally stopping the spiral clears both switches
// Repeatedly sending the start spiral command toggles the spiralling state
// Therefore to be sure a spiral has started, send the stop command and then the start
bool LX200NYX101::StartSpiral()
{
	IPState state = IPS_BUSY;
	if (sendCommand(":Mp#"))
	{
		state = IPS_OK;
	}
	else
	{
		SpiralSP.setState(IPS_ALERT);
		return false;
	}
	SpiralSP.setState(state);
	return true;
}

bool LX200NYX101::StopSpiral()
{
	IPState state = IPS_BUSY;
	if (sendCommand(":Q#"))
	{
		state = IPS_OK;
	}
	else
	{
		SpiralSP.setState(IPS_ALERT);
		return false;
	}
	IUResetSwitch(SpiralSP);
	SpiralSP.setState(state);
	IDSetSwitch(SpiralSP, nullptr);
	return true;
}


bool LX200NYX101::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        // Slew rates
        if (RateNP.isNameMatch(name))
        {
            RateNP.update(values, names, n);
            IPState state = IPS_OK;
            IPState state_1 = IPS_OK;
            IPState state_2 = IPS_OK;
            if (isConnected())
                {
                    state_1 = SetSlewRateRA(values[RA]) ? IPS_OK : IPS_ALERT;
                    state_2 = SetSlewRateDEC(values[DEC]) ? IPS_OK : IPS_ALERT;
                    if (state_1 == IPS_OK && state_2 == IPS_OK)
                    {
                        state = IPS_OK;
                    }
                    else
                    {
                        state = IPS_ALERT;
                    }
                }
            RateNP.setState(state);
            RateNP.apply();
        }
        
        // Meridian limit
        if (MeridianLimitNP.isNameMatch(name))
        {
            MeridianLimitNP.update(values, names, n);
            if (!isSimulation())
            {
                std::string command = ":SXE9," + std::to_string(MeridianLimitNP[0].getValue()) + "#";;
                sendCommand(command.c_str());
                command = ":SXEA," + std::to_string(MeridianLimitNP[0].getValue()) + "#";;
                sendCommand(command.c_str());
                if (MeridianLimitNP.getState() == IPS_OK)
                {
                    LOGF_INFO("Meridian!: ",  MeridianLimitNP[0].getValue());
                    
                }
            }
            else
            {
                MeridianLimitNP.setState(IPS_OK);
            }
            
            MeridianLimitNP.apply();
            return true;
        }

        // Meridian limit
        if (ElevationLimitNP.isNameMatch(name))
        {
            if(ElevationLimitNP.update(values, names, n))
            {
                for(int i = 0; i<n; ++i)
                {
                    if(ElevationLimitNP[OVERHEAD].isNameMatch(names[i]))
                    {
                        std::string command = ":So" + std::to_string(static_cast<int>(ElevationLimitNP[OVERHEAD].getValue())) + "#";;
                        sendCommand(command.c_str());
                    }
                    else if(ElevationLimitNP[HORIZON].isNameMatch(names[i]))
                    {
                        std::string command = ":Sh" + std::to_string(static_cast<int>(ElevationLimitNP[HORIZON].getValue())) + "#";;
                        sendCommand(command.c_str());
                    }
                }
                ElevationLimitNP.apply();
                return true;
            }
        }
    }
    return LX200Generic::ISNewNumber(dev, name, values, names, n);
}


 

bool LX200NYX101::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        // Mount Type
        if (MountTypeSP.isNameMatch(name))
        {
            int previousType = MountTypeSP.findOnSwitchIndex();
            MountTypeSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                auto targetType = MountTypeSP.findOnSwitchIndex();
                state = setMountType(targetType) ? IPS_OK : IPS_ALERT;
                if (state == IPS_OK && previousType != targetType)
                    LOG_WARN("Restart mount in order to apply changes to Mount Type.");
            }
            MountTypeSP.setState(state);
            MountTypeSP.apply();
            return true;
        }
        else if(GuideRateSP.isNameMatch(name))
        {
            int previousType = GuideRateSP.findOnSwitchIndex();
            GuideRateSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                auto targetType = GuideRateSP.findOnSwitchIndex();
                state = setGuideRate(targetType) ? IPS_OK : IPS_ALERT;
                if (state == IPS_OK && previousType != targetType)
                    LOG_WARN("RA and DEC guide rate changed.");
            }
            GuideRateSP.setState(state);
            GuideRateSP.apply();
            return true;
        }
        else if(HomeSP.isNameMatch(name))
        {
            HomeSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                HomeSP[0].setState(ISS_OFF);
                sendCommand(":hC#");
            }
            HomeSP.setState(state);
            HomeSP.apply();
            return true;
        }
        else if(FlipSP.isNameMatch(name))
        {
            FlipSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                FlipSP[0].setState(ISS_OFF);
                sendCommand(":MN#");
            }
            FlipSP.setState(state);
            FlipSP.apply();
            return true;
        }
        else if(RebootSP.isNameMatch(name))
        {
            RebootSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                RebootSP[0].setState(ISS_OFF);
                sendCommand(":ERESET#");
            }
            RebootSP.setState(state);
            RebootSP.apply();
            return true;
        }
        else if(ResetHomeSP.isNameMatch(name))
        {
            ResetHomeSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                ResetHomeSP[0].setState(ISS_OFF);
                sendCommand(":hF#");
            }
            ResetHomeSP.setState(state);
            ResetHomeSP.apply();
            return true;
        }
		// Set the current position as the park position
		else if(SetParkSP.isNameMatch(name))
        {
            SetParkSP.update(states, names, n);
            IPState state = IPS_OK;
            if (isConnected())
            {
                SetParkSP[0].setState(ISS_OFF);
                sendCommand(":hQ#");
            }
			else
			{
				state = IPS_ALERT;
			}
            SetParkSP.setState(state);
            SetParkSP.apply();
            return true;
        }
        else if(SafetyLimitSP.isNameMatch(name))
        {
            SafetyLimitSP.update(states, names, n);
            auto index = SafetyLimitSP.findOnSwitchIndex();
            switch(index){
            case SET_SAFETY_LIMIT:
                sendCommand(":Sc1#");
                sendCommand(":Sc#");
                break;
            case CLEAR_SAFETY_LIMIT:
                sendCommand(":Sc0#");
                sendCommand(":Sc#");
                break;
            }
            SafetyLimitSP.apply();
            return true;
        }
        else if(RefractSP.isNameMatch(name))
        {
            RefractSP.update(states, names, n);
            auto index = RefractSP.findOnSwitchIndex();

            switch(index){
            case REFRACT_ON:
                sendCommand(":Tr#");
                break;
            case REFRACT_OFF:
                sendCommand(":Tn#");
                break;
            }
            RefractSP.apply();
            return true;
        }
		else if (SpiralSP.isNameMatch(name))
		{
			SpiralSP.update(states, names, n);
			auto index = SpiralSP.findOnSwitchIndex();
            switch(index)
			{
				case START:
					StartSpiral();
					break;
				case STOP:
					StopSpiral();
					break;
            }
            SpiralSP.apply();
			return true;
		}

    }
    return LX200Generic::ISNewSwitch(dev, name, states, names, n);
}

#ifdef DEBUG_NYX
bool LX200NYX101::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (DebugCommandTP.isNameMatch(name))
        {
            DebugCommandTP.update(texts, names, n);
            for(int i = 0; i<n; i++)
            {
                if(DebugCommandTP[0].isNameMatch(names[i]))
                {
                    char status[DRIVER_LEN] = {0};
                    sendCommand(texts[i]);
                    i=n;
                }
            }
            return true;
        }
    }
    return LX200Generic::ISNewText(dev, name, texts, names, n);
    
}
#endif

bool LX200NYX101::SetSlewRate(int index)
{
    double value = 0.0;
    switch(index)
    {
    case 0:
        value = 0.01;
        break;
    case 1:
        value = 0.03;
        break;
    case 2:
        value = 0.07;
        break;
    case 3:
        value = 0.27;
        break;
    case 4:
        value = 0.50;
        break;
    case 5:
        value = 0.65;
        break;
    case 6:
        value = 0.80;
        break;
    case 7:
        value = 1;
        break;
    case 8:
        value = 2.5;
        break;
    case 9:
        value = 5;
        break;
    };

    char decCommand[DRIVER_LEN] = {0};
    snprintf(decCommand, DRIVER_LEN, ":RE%f#", value);

    char raCommand[DRIVER_LEN] = {0};
    snprintf(raCommand, DRIVER_LEN, ":RA%f#", value);

    return sendCommand(decCommand) && sendCommand(raCommand);
}

bool LX200NYX101::SetSlewRateRA(double value)
{
    char raSlewCommand[DRIVER_LEN] = {0};
    char raCommand[DRIVER_LEN] = {0};
    if (value == 0){
        snprintf(raSlewCommand, DRIVER_LEN, ":Qw#");
    }
    else if (value > 0){
        snprintf(raSlewCommand, DRIVER_LEN, ":Mw#");
    }
    else if (value < 0){
        snprintf(raSlewCommand, DRIVER_LEN, ":Me#");
    }
    snprintf(raCommand, DRIVER_LEN, ":RA%f#", abs(value));
    

    return sendCommand(raCommand) && sendCommand(raSlewCommand);
}

bool LX200NYX101::SetSlewRateDEC(double value)
{

    char decSlewCommand[DRIVER_LEN] = {0};
    char decCommand[DRIVER_LEN] = {0};

    if (value == 0){
        snprintf(decSlewCommand, DRIVER_LEN, ":Qn#");
    }
    else if (value > 0){
        snprintf(decSlewCommand, DRIVER_LEN, ":Mn#");
    }
    else if (value < 0){
        snprintf(decSlewCommand, DRIVER_LEN, ":Ms#");
    }
    snprintf(decCommand, DRIVER_LEN, ":RE%f#", abs(value));
    

    return sendCommand(decCommand) && sendCommand(decSlewCommand);
}

bool LX200NYX101::setGuideRate(int rate)
{
    char command[DRIVER_LEN] = {0};
    snprintf(command, DRIVER_LEN, ":R%d#", rate);
    return sendCommand(command);
}

bool LX200NYX101::SetTrackMode(uint8_t mode)
{
    switch(mode){
    case TRACK_SIDEREAL:
        return sendCommand(":TQ#");
        break;
    case TRACK_SOLAR:
        return sendCommand(":TS#");
        break;
    case TRACK_LUNAR:
        return sendCommand(":TL#");
        break;
    case TRACK_KING:
        return sendCommand(":TK#");
        break;
    }
    return false;
}

bool LX200NYX101::setMountType(int type)
{
    return sendCommand((type == Equatorial) ? ":SXEM,1#" : "::SXEM,3#");
}

bool LX200NYX101::goToPark()
{
    LOG_INFO("Park requested.");
    return sendCommand(":hP#");
}

bool LX200NYX101::goToUnPark()
{
    return sendCommand(":hR#");
}

bool LX200NYX101::Park()
{
    bool rc = goToPark();
    if (rc)
        TrackState = SCOPE_PARKING;

    return rc;
}

bool LX200NYX101::UnPark()
{
    bool rc = goToUnPark();
    if(rc)
        SetParked(false);

    return rc;
}

bool LX200NYX101::SetTrackEnabled(bool enabled)
{
    char response[DRIVER_LEN] = {0};
    bool rc = sendCommand(enabled ? ":Te#" : ":Td#", response, 4, 1);
    return rc && response[0] == '1';
}

bool LX200NYX101::setUTCOffset(double offset)
{
    int h {0}, m {0}, s {0};
    char command[DRIVER_LEN] = {0};
    offset *= -1;
    getSexComponents(offset, &h, &m, &s);

    snprintf(command, DRIVER_LEN, ":SG%c%02d:%02d#", offset >= 0 ? '+' : '-', std::abs(h), m);
    return setStandardProcedure(PortFD, command) == 0;
}

bool LX200NYX101::setLocalDate(uint8_t days, uint8_t months, uint16_t years)
{
    char command[DRIVER_LEN] = {0};
    snprintf(command, DRIVER_LEN, ":SC%02d/%02d/%02d#", months, days, years % 100);
    return setStandardProcedure(PortFD, command) == 0;
}

bool LX200NYX101::updateLocation(double latitude, double longitude, double elevation)
{
    INDI_UNUSED(elevation);
    int d{0}, m{0}, s{0};

    // JM 2021-04-10: MUST convert from INDI longitude to standard longitude.
    // DO NOT REMOVE
    if (longitude > 180)
        longitude = longitude - 360;

    char command[DRIVER_LEN] = {0};
    // Reverse as per Meade way
    longitude *= -1;
    getSexComponents(longitude, &d, &m, &s);
    snprintf(command, DRIVER_LEN, ":Sg%c%03d*%02d:%02d#", longitude >= 0 ? '+' : '-', std::abs(d), m, s);
    if (setStandardProcedure(PortFD, command) < 0)
    {
        LOG_ERROR("Error setting site longitude coordinates");
        return false;
    }

    getSexComponents(latitude, &d, &m, &s);
    snprintf(command, DRIVER_LEN, ":St%c%02d*%02d:%02d#", latitude >= 0 ? '+' : '-', std::abs(d), m, s);
    if (setStandardProcedure(PortFD, command) < 0)
    {
        LOG_ERROR("Error setting site latitude coordinates");
        return false;
    }

    return true;
}


bool LX200NYX101::sendCommand(const char * cmd, char * res, int cmd_len, int res_len)
{
    int nbytes_written = 0, nbytes_read = 0, rc = -1;

    tcflush(PortFD, TCIOFLUSH);

    if (cmd_len > 0)
    {
        char hex_cmd[DRIVER_LEN * 3] = {0};
        hexDump(hex_cmd, cmd, cmd_len);
        LOGF_DEBUG("CMD <%s>", hex_cmd);
        rc = tty_write(PortFD, cmd, cmd_len, &nbytes_written);
    }
    else
    {
        LOGF_DEBUG("CMD <%s>", cmd);
        rc = tty_write_string(PortFD, cmd, &nbytes_written);
    }

    if (rc != TTY_OK)
    {
        char errstr[MAXRBUF] = {0};
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Serial write error: %s.", errstr);
        return false;
    }

    if (res == nullptr)
    {
        tcdrain(PortFD);
        return true;
    }

    if (res_len > 0)
        rc = tty_read(PortFD, res, res_len, DRIVER_TIMEOUT, &nbytes_read);
    else
        rc = tty_nread_section(PortFD, res, DRIVER_LEN, DRIVER_STOP_CHAR, DRIVER_TIMEOUT, &nbytes_read);

    if (rc != TTY_OK)
    {
        char errstr[MAXRBUF] = {0};
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Serial read error: %s.", errstr);
        return false;
    }

    if (res_len > 0)
    {
        char hex_res[DRIVER_LEN * 3] = {0};
        hexDump(hex_res, res, res_len);
        LOGF_DEBUG("RES <%s>", hex_res);
    }
    else
    {
        LOGF_DEBUG("RES <%s>", res);
    }

    tcflush(PortFD, TCIOFLUSH);

    return true;
}

void LX200NYX101::hexDump(char * buf, const char * data, int size)
{
    for (int i = 0; i < size; i++)
        sprintf(buf + 3 * i, "%02X ", static_cast<uint8_t>(data[i]));

    if (size > 0)
        buf[3 * size - 1] = '\0';
}

std::vector<std::string> LX200NYX101::split(const std::string &input, const std::string &regex)
{
    // passing -1 as the submatch index parameter performs splitting
    std::regex re(regex);
    std::sregex_token_iterator
    first{input.begin(), input.end(), re, -1},
          last;
    return {first, last};
}




