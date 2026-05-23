#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_read_params read_params;

enum {
    OS_TYPE_WIN,
    OS_TYPE_MAC,
};
static struct bt_conn * default_conn;
static uint8_t os_type;
static bool manf_found;
static bool pnp_start;
void start_discover_worker(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(start_discover_work, start_discover_worker);
static uint8_t read_manf_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
    const void *data, uint16_t length);
static uint8_t read_pnp_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
    const void *data, uint16_t length) {
    LOG_DBG("Read err: %d, length %d,%s", err, length,(char*)data);
    uint8_t *p_data =(uint8_t *)data;
    LOG_HEXDUMP_DBG(p_data,length,"pnp");
    if(p_data[0]==1 && p_data[1]==6 && p_data[2]==0)
    {
        os_type = OS_TYPE_WIN;
        LOG_DBG("os WIN found");
    }
    return BT_GATT_ITER_STOP;
}
static uint8_t read_manf_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
    const void *data, uint16_t length) {
    LOG_DBG("Read err: %d, length %d,%s", err, length,(char*)data);
    uint8_t *p_data =(uint8_t *)data;
    // LOG_HEXDUMP_DBG(p_data,length,"manf");
    if(memcmp(p_data,"Apple",5)==0)
    {
        os_type =OS_TYPE_MAC;
        LOG_DBG("os mac found");
    }

    return BT_GATT_ITER_STOP;
}

static uint8_t discover_func(struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    struct bt_gatt_discover_params *params)
{
    int err;
    
    if (!attr) {
        LOG_DBG("Discover complete");
        // (void)memset(params, 0, sizeof(*params));
        if(!manf_found && !pnp_start)
        {
            pnp_start=true;
            LOG_DBG("pnp start check");
            memcpy(&discover_uuid, BT_UUID_DIS_PNP_ID, sizeof(discover_uuid));
            discover_params.uuid = &discover_uuid.uuid;
            discover_params.start_handle = 1;
            discover_params.end_handle =0xffff;
            discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

            err = bt_gatt_discover(conn, &discover_params);
            if (err) {
                LOG_DBG("Discover failed (err %d)\n", err);
            }
        }
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[ATTRIBUTE] handle %u", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_DIS)) {
        LOG_DBG("service found");
        memcpy(&discover_uuid, BT_UUID_DIS_MANUFACTURER_NAME, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle+1;
        discover_params.end_handle =0xffff;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_DBG("Discover failed (err %d)\n", err);
        }
    } else if (!bt_uuid_cmp(discover_params.uuid,
        BT_UUID_DIS_MANUFACTURER_NAME)) {
        LOG_DBG("manf char found");
        manf_found =true;
        read_params.single.handle = bt_gatt_attr_value_handle(attr);;
        read_params.single.offset = 0;
        read_params.handle_count = 1;
        read_params.func = read_manf_cb;

        bt_gatt_read(conn, &read_params);

    }else if (!bt_uuid_cmp(discover_params.uuid,
        BT_UUID_DIS_PNP_ID)) {
        LOG_DBG("pnp char found");
        read_params.single.handle = bt_gatt_attr_value_handle(attr);;
        read_params.single.offset = 0;
        read_params.handle_count = 1;
        read_params.func = read_pnp_cb;

        bt_gatt_read(conn, &read_params);

    }  


    return BT_GATT_ITER_STOP;
}

void start_discover(struct bt_conn * conn,uint32_t delay)
{
    default_conn =conn;
    k_work_reschedule(&start_discover_work,K_MSEC(delay));
    LOG_ERR("start discover delay:%d ms",delay);
}
void start_discover_worker(struct k_work *work)
{
    manf_found =false;
    pnp_start =false;
    memcpy(&discover_uuid, BT_UUID_DIS, sizeof(discover_uuid));
    discover_params.uuid = &discover_uuid.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;
    LOG_DBG("start discover");
    int err = bt_gatt_discover(default_conn, &discover_params);
    if (err) {
        LOG_DBG("Discover failed(err %d)\n", err);
        return;
    }
}
uint8_t get_os_type(void)
{
    return os_type;
}