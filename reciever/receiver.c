#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <MQTTClient.h>

#define ADDRESS     "broker.hivemq.com:1883"
#define CLIENTID    "ExampleClientSub"
#define TOPIC       "MQTT Examples"
#define PAYLOAD     "Hello World!"
#define QOS         1
#define TIMEOUT     10000L
 
volatile MQTTClient_deliveryToken deliveredtoken;
 
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}
 
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: %.*s\n", message->payloadlen, (char*)message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}
 
void connlost(void *context, char *cause)
{
    printf("\nConnection lost\n");
    printf("     cause: %s\n", cause);
}
void do_exit(PGconn *conn) {
    
    PQfinish(conn);
    exit(1);
}

void display_all_vehicles(PGconn *conn){

    PGresult *res = PQexec(conn, "SELECT * FROM vehicle");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {

        printf("No data retrieved\n");        
        PQclear(res);
        do_exit(conn);
    }    
    
    int rows = PQntuples(res);
    
    for(int i=0; i<rows; i++) {
        
        printf("%s %s %s %s\n", PQgetvalue(res, i, 0), 
        PQgetvalue(res, i, 1), PQgetvalue(res, i, 2),PQgetvalue(res, i, 3));
    }    

    PQclear(res);

}

void insert_vehicle(PGconn *conn, char *title, char *color, char *num_plate){

    const char *query = "INSERT INTO vehicle (title, color, num_plate) VALUES ($1, $2, $3)";

    // Prepare the statement
    PGresult *prepare_res = PQprepare(conn, "insert_stmt", query, 3, NULL);
    if (PQresultStatus(prepare_res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Prepare statement failed: %s", PQerrorMessage(conn));
        PQclear(prepare_res);
        PQfinish(conn);
    }
    // Bind and execute the prepared statement
    const char *paramValues[3] = { title, color, num_plate };
    int paramLengths[3] = { strlen(title), strlen(color), strlen(num_plate) };
    int paramFormats[3] = { 0, 0, 0 };  // 0 for text

    PGresult *exec_res = PQexecPrepared(conn, "insert_stmt", 3, paramValues, paramLengths, paramFormats, 0);
    if (PQresultStatus(exec_res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Execute statement failed: %s", PQerrorMessage(conn));
        PQclear(exec_res);
        PQfinish(conn);
    }
    PQclear(prepare_res);
    PQclear(exec_res);

    printf("Data inserted successfully.\n");
}

int main() {
    
    PGconn *conn = PQconnectdb("user=postgres password=postgres host=winhost port=5432 dbname=trafficflow");
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }

    int ver = PQserverVersion(conn);

    PGresult *tables = PQexec(conn, "SELECT * FROM pg_catalog.pg_tables");
    printf("%s\n",PQdb(conn));

    printf("%s\n", PQgetvalue(tables,0,0));
    //PQclear(tables);
        // Input values from stdin
    // char title[100], color[100], num_plate[20];

    // printf("Enter Title: ");
    // scanf("%99s", title);
    // printf("Enter Color: ");
    // scanf("%99s", color);
    // printf("Enter Number Plate: ");
    // scanf("%19s", num_plate);

    int rc;
 
    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to create client, return code %d\n", rc);
        rc = EXIT_FAILURE;
        return rc;
    }
 
    if ((rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to set callbacks, return code %d\n", rc);
        rc = EXIT_FAILURE;
        return rc;
    }
 
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        rc = EXIT_FAILURE;
        MQTTClient_destroy(&client);;
    }
 
    printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n"
           "Press Q<Enter> to quit\n\n", TOPIC, CLIENTID, QOS);
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to subscribe, return code %d\n", rc);
        rc = EXIT_FAILURE;
    }
    else
    {
        int ch;
        do
        {
                ch = getchar();
        } while (ch!='Q' && ch != 'q');
 
        if ((rc = MQTTClient_unsubscribe(client, TOPIC)) != MQTTCLIENT_SUCCESS)
        {
                printf("Failed to unsubscribe, return code %d\n", rc);
                rc = EXIT_FAILURE;
        }
    }
 
    if ((rc = MQTTClient_disconnect(client, 10000)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to disconnect, return code %d\n", rc);
        rc = EXIT_FAILURE;
    }

    // insert_vehicle(conn,title,color,num_plate);
    PQfinish(conn);
    return 0;
}