/*
 * FileName:    Nvm_Mng.c
 * Brief:       Non volatile memory manager
 */
#include "ets_sys.h"
#include "os_type.h"
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
#include "mqtt.h"
#include "user_config.h"
#include "debug.h"
#include "Nvm_Mng.h"

t_Nvm_Mng_Data Nvm_Mng_Data;
SAVE_FLAG saveFlag;
t_timer nvm_save_timer = { .timer = 0, .expired = TRUE, .name = "NVM save timer" };

/*
 * Name:    void Nvm_Mng_Init (void)
 * Descr:   Initializa nvm manager
 */

void ICACHE_FLASH_ATTR Nvm_Mng_Init(void)
{

}

/*
 * Name:    void Nvm_Mng_Save (void)
 * Descr:   Savem to flash
 */

void real_NVM_save(void)
{
    DEBUG_INFO("Saving to NVM...");
    //SET_HIGH(YELLOW_GPIO);
    spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE, (uint32 *)&saveFlag, sizeof(SAVE_FLAG));

    if (saveFlag.flag == 0)
    {
        //SET_HIGH(CYAN_GPIO);

        //SET_HIGH(MAGENTA_GPIO);
        spi_flash_erase_sector(CFG_LOCATION + 1);
        //SET_LOW(MAGENTA_GPIO);
        //SET_HIGH(BLUE_GPIO);
        spi_flash_write((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE, (uint32 *)&Nvm_Mng_Data, sizeof(t_Nvm_Mng_Data));
        //SET_LOW(BLUE_GPIO);
        saveFlag.flag = 1;
        //SET_HIGH(MAGENTA_GPIO);
        spi_flash_erase_sector(CFG_LOCATION + 3);
        //SET_LOW(MAGENTA_GPIO);
        //SET_HIGH(BLUE_GPIO);
        spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE, (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
        //SET_LOW(BLUE_GPIO);

        //SET_LOW(CYAN_GPIO);
    }
    else
    {
        //SET_HIGH(MAGENTA_GPIO);
        spi_flash_erase_sector(CFG_LOCATION + 0);
        //SET_LOW(MAGENTA_GPIO);
        //SET_HIGH(BLUE_GPIO);
        spi_flash_write((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE, (uint32 *)&Nvm_Mng_Data, sizeof(t_Nvm_Mng_Data));
        //SET_LOW(BLUE_GPIO);
        saveFlag.flag = 0;
        //SET_HIGH(MAGENTA_GPIO);
        spi_flash_erase_sector(CFG_LOCATION + 3);
        //SET_LOW(MAGENTA_GPIO);
        //SET_HIGH(BLUE_GPIO);
        spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE, (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
        //SET_LOW(BLUE_GPIO);
    }

    //SET_LOW(YELLOW_GPIO);
    DEBUG_INFO("NVM saved!");
}

void check_for_NVM_save(void)
{
    if (nvm_save_timer.expired)
        return;

    if (nvm_save_timer.timer == MAX_NVM_SAVE_TIME)
    {
        real_NVM_save();
        nvm_save_timer.expired = TRUE;
        return;
    }

    nvm_save_timer.timer += CYCLE_TIME;
}

void Nvm_Mng_Save(void)
{
    nvm_save_timer.timer = 0;
    nvm_save_timer.expired = FALSE;
}

/*
 * Name:    bool Nvm_Mng_Load (void)
 * Descr:   Load from flash
 * Return:  TRUE if load is from CFG; FALSE otherwise
 */

bool ICACHE_FLASH_ATTR Nvm_Mng_Load (void)
{
    uint8_t mac[6];

    spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
    if (saveFlag.flag == 0)
    {
        spi_flash_read((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)&Nvm_Mng_Data, sizeof(t_Nvm_Mng_Data));
    }
    else
    {
        spi_flash_read((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)&Nvm_Mng_Data, sizeof(t_Nvm_Mng_Data));
    }

    if(Nvm_Mng_Data.m_cfg_holder != CFG_HOLDER)
    {
        Nvm_Mng_Data.m_cfg_holder = CFG_HOLDER;

        /* Start in config mode */
        Nvm_Mng_Data.m_boot_mode = CONFIG_BOOTMODE_CONFIG;

        wifi_get_macaddr(STATION_IF, mac);
        os_sprintf(Nvm_Mng_Data.m_device_id, MACSTR, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ERROR_INFO("m_device_id: %s", Nvm_Mng_Data.m_device_id);

        os_strncpy(Nvm_Mng_Data.m_sta_ssid, STA_SSID, sizeof(Nvm_Mng_Data.m_sta_ssid) - 1);
        os_strncpy(Nvm_Mng_Data.m_sta_pwd, STA_PASS, sizeof(Nvm_Mng_Data.m_sta_pwd) - 1);
        Nvm_Mng_Data.m_sta_type = STA_TYPE;

        os_strncpy(Nvm_Mng_Data.m_mqtt_host, MQTT_HOST, sizeof(Nvm_Mng_Data.m_mqtt_host) - 1);
        Nvm_Mng_Data.m_mqtt_port = MQTT_PORT;
        os_strncpy(Nvm_Mng_Data.m_mqtt_user, MQTT_USER, sizeof(Nvm_Mng_Data.m_mqtt_user) - 1);
        os_strncpy(Nvm_Mng_Data.m_mqtt_pass, MQTT_PASS, sizeof(Nvm_Mng_Data.m_mqtt_pass) - 1);
        
        char char_type = Nvm_Mng_read_type();

        if (char_type == 'S') 
        {
            Nvm_Mng_Data.m_type = CONFIG_TYPE_NORMAL_LIGHT;
        }
        else if (char_type == 'R') 
        {
            Nvm_Mng_Data.m_type = CONFIG_TYPE_NORMAL_ROLLER;
        }
        else 
        {
            ERROR_INFO("Type not known use the default one: CONFIG_TYPE_NORMAL_LIGHT");
            Nvm_Mng_Data.m_type = CONFIG_TYPE_NORMAL_LIGHT;
        }   

        Nvm_Mng_Data.m_roll_totlen          = 3000;
        Nvm_Mng_Data.m_rise_steps           = Nvm_Mng_Data.m_roll_totlen/CYCLE_TIME;
        Nvm_Mng_Data.m_fall_steps           = Nvm_Mng_Data.m_roll_totlen/CYCLE_TIME;
        Nvm_Mng_Data.m_roll_curr_perc       = 100;
        Nvm_Mng_Data.m_relecurrstatus_ch1   = 0;
        Nvm_Mng_Data.m_numberstatus_ch1     = 0;
        Nvm_Mng_Data.m_reletmr_ch1          = 0;
        Nvm_Mng_Data.m_relecurrstatus_ch2   = 0;
        Nvm_Mng_Data.m_numberstatus_ch2     = 0;
        Nvm_Mng_Data.m_reletmr_ch2          = 0;

        /* Initialize all pins to zero */
        uint8_t i;
        for (i = 0; i < GPIO_MNG_PIN_MAX_NUM; i++)
            Nvm_Mng_Data.m_statuspin[i] = 0;

        os_strncpy(Nvm_Mng_Data.m_device_id_rem, REMOTE_DEF, sizeof(Nvm_Mng_Data.m_device_id_rem) - 1);
        Nvm_Mng_Data.m_security = DEFAULT_SECURITY; /* default ssl */
        Nvm_Mng_Data.m_mqtt_keepalive = MQTT_KEEPALIVE_DEFAULT;
        Nvm_Mng_Data.m_pam_mode = PAM_ENABLE;
        Nvm_Mng_Data.m_rebooted = 0;
        Nvm_Mng_Data.m_config_version = 0;
        
        for (i = 0; i < 2; i++)
        {
            if (Nvm_Mng_Data.m_relay_counter[i] == -1)
                Nvm_Mng_Data.m_relay_counter[i] = 0;

            Nvm_Mng_Data.m_relay[i] = TRUE;

            if (Nvm_Mng_Data.m_dimming_perc[i] == -1)
                Nvm_Mng_Data.m_dimming_perc[i] = 0;
            Nvm_Mng_Data.m_dimming_steps[i] = 0;
        }

        Nvm_Mng_Data.m_mqtt_loop_counter = 0;
        Nvm_Mng_Data.m_backoff = MIN_BACKOFF_TIMER;
        Nvm_Mng_Data.m_need_feedback = FALSE;
        Nvm_Mng_Data.m_inhibit_max_time = 5;
        Nvm_Mng_Data.m_fota_status = FOTA_STATUS_OK;
        Nvm_Mng_Data.m_rollback_counter = 0;

        Nvm_Mng_Save();

        /* Load is from CFG */
        return TRUE;
    }

    /* Load is from NVM */
    return FALSE;
}

/*
 * Name:    t_Nvm_Mng_Data *Nvm_Mng_GetNvm (void)
 * Descr:   Return pointer to Nvm
 */

t_Nvm_Mng_Data* ICACHE_FLASH_ATTR Nvm_Mng_GetNvm(void)
{
    return (t_Nvm_Mng_Data*)&Nvm_Mng_Data;
}

uint8_t ICACHE_FLASH_ATTR Nvm_Mng_read_type(void)
{
    uint32_t c;
    uint8_t *addr = (uint8_t *)&c;

    spi_flash_read(0xF7 * SPI_FLASH_SEC_SIZE, (uint32_t *)addr, 1);

    if ((addr[0] != 82) && (addr[0] != 83))
        return 0;

    return addr[0];
}

bool ICACHE_FLASH_ATTR read_from_flash(uint32_t address, uint8_t *from_flash, uint32_t len)
{
    uint16_t sector = (address >> 12);

    int read_res = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, (uint32_t *)from_flash, len);

    if (read_res)
    {
        ERROR_INFO("read_from_flash | error reading %d bytes from address 0x%08X [read_res: %d]", len, address, read_res);
        return FALSE;
    }

    DEBUG_INFO("Success in reading %d bytes from address 0x%08X", len, address);
    return TRUE;
}

bool ICACHE_FLASH_ATTR write_to_flash(uint8_t *to_flash, uint32_t address, uint32_t len)
{
    uint16_t sector = (address >> 12);

    int erase_res = spi_flash_erase_sector(sector);
    int write_res = spi_flash_write(sector * SPI_FLASH_SEC_SIZE, (uint32_t *)to_flash, len);

    if (erase_res || write_res)
    {
        if (erase_res != 0)
            ERROR_INFO("write_to_flash | error erasing sector %d containing 0x%08X [erase_res: %d]", sector, address, erase_res);
        if (write_res != 0)
            ERROR_INFO("write_to_flash | error writing sector %d containing 0x%08X [write_res: %d]", sector, address, write_res);

        return FALSE;
    }

    DEBUG_INFO("Success in erasing and writing %d bytes to address 0x%08X", len, address);
    os_free(to_flash);
    return TRUE;
}
