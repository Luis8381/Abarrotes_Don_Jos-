// =============================================================================
// PUNTO DE VENTA - Abarrotera Don José
// Qt + MariaDB Connector/C
//
// Pantallas: Login, Ventas, Inventario, Clientes, Usuarios (admin), Reportes
// Roles:     administrador (todo)  |  vendedor (ventas, consulta, clientes, sus reportes)
//
// Requiere haber ejecutado migracion.sql sobre la base de datos.
// La conexión al servidor se configura en config.ini (junto al .exe).
//
// CÓMO LEER ESTE ARCHIVO
//   * Los botones, campos y tablas son variables normales de cada clase
//     (QPushButton btnCobrar;) y se usan con punto:  btnCobrar.setText("...").
//   * &btnCobrar significa "la dirección de btnCobrar" (Qt la pide para
//     conectar o acomodar el widget). "this" significa "esta ventana".
//   * Cada botón se conecta a una función CON NOMBRE. Todas las conexiones de
//     una pantalla están juntas en la función conectarEventos(), y cada función
//     que responde a un botón dice arriba "Se llama desde: ...".
//   * Los punteros que Qt y MariaDB necesitan por dentro están escondidos en las
//     secciones 2 (base de datos) y 4 (ayudas de interfaz).
//
// ORDEN DE LAS VARIABLES (importante): dentro de una clase, un contenedor
// (QGroupBox, QFrame, panel) se declara ANTES que los widgets que contiene.
// =============================================================================

#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDateEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QGroupBox>
#include <QDialog>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QDateTime>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include <QDir>
#include <QFrame>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QSettings>
#include <QProcessEnvironment>
#include <mysql.h>

// =============================================================================
// 1. VARIABLES GLOBALES
// =============================================================================
const int STOCK_BAJO = 10;   // existencia menor o igual a esto = "bajo"
const int TAM_ICONO = 104;   // tamaño de las imágenes del catálogo
const QString NOMBRE_NEGOCIO = "Abarrotera Don José";

MYSQL *conexion = nullptr;   // conexión a la base de datos (solo se usa en la sección 2)

// Datos del usuario que inició sesión
int sesionId = 0;
QString sesionUsuario;
QString sesionRol;

bool esAdmin() {
    return sesionRol == "administrador";
}

// =============================================================================
// 2. BASE DE DATOS
// Aquí están todos los punteros de MariaDB. Las pantallas solo llaman a estas
// funciones y nunca tocan "conexion" directamente.
// =============================================================================

// Escapa texto para usarlo en SQL (evita inyección SQL)
QString esc(const QString &texto) {
    QByteArray bytes = texto.toUtf8();
    QByteArray salida(bytes.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(conexion, salida.data(), bytes.constData(), bytes.size());
    return QString::fromUtf8(salida.constData(), int(n));
}

// Devuelve 'texto' entre comillas, o NULL si está vacío
QString sqlTxt(const QString &texto) {
    if (texto.trimmed().isEmpty())
        return "NULL";
    return "'" + esc(texto.trimmed()) + "'";
}

QString errorBD() {
    return QString::fromUtf8(mysql_error(conexion));
}

// Ejecuta una instrucción (INSERT, UPDATE, DELETE...). Devuelve true si funcionó
bool ejecutar(const QString &sql) {
    if (mysql_query(conexion, sql.toUtf8().constData()) != 0) {
        qWarning() << "Error SQL:" << errorBD() << "en:" << sql;
        return false;
    }
    return true;
}

// Ejecuta un SELECT y devuelve las filas como lista de listas de texto
QVector<QStringList> consultar(const QString &sql) {
    QVector<QStringList> filas;
    if (!ejecutar(sql))
        return filas;

    MYSQL_RES *resultado = mysql_store_result(conexion);
    if (resultado == nullptr)
        return filas;

    unsigned int columnas = mysql_num_fields(resultado);
    MYSQL_ROW registro;
    while ((registro = mysql_fetch_row(resultado))) {
        QStringList fila;
        for (unsigned int i = 0; i < columnas; i++) {
            if (registro[i])
                fila << QString::fromUtf8(registro[i]);
            else
                fila << QString();
        }
        filas.push_back(fila);
    }
    mysql_free_result(resultado);
    return filas;
}

// Número (id) que la base le dio al último INSERT. Sirve para el folio de la venta
qulonglong ultimoIdInsertado() {
    return mysql_insert_id(conexion);
}

// Cuántas filas cambió el último UPDATE
qulonglong filasAfectadas() {
    return mysql_affected_rows(conexion);
}

void cerrarConexion() {
    mysql_close(conexion);
}

// Lee un dato de conexión. Orden de prioridad:
//   1) variable de entorno (DB_HOST, DB_USER...)
//   2) archivo config.ini junto al ejecutable
//   3) valor por defecto
QString leerConfig(const QString &clave, const QString &variableEntorno, const QString &porDefecto) {
    QString deEntorno = QProcessEnvironment::systemEnvironment().value(variableEntorno);
    if (!deEntorno.isEmpty())
        return deEntorno;

    QSettings ini(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    QVariant valor = ini.value("basedatos/" + clave);
    if (valor.userType() == QMetaType::QStringList)   // una clave con comas se lee como lista
        return valor.toStringList().join(",");
    if (valor.isValid() && !valor.toString().isEmpty())
        return valor.toString();

    return porDefecto;
}

// Intenta conectarse al servidor. Devuelve true si lo logró.
// En el plan gratis la base se "duerme" si no se usa: el primer intento puede tardar
// o fallar mientras despierta, por eso se espera hasta 30 segundos y se reintenta una vez.
bool conectarBaseDatos(const QString &host, int puerto, const QString &usuario,
                       const QString &password, const QString &base, QString &detalleError) {
    QByteArray h = host.toUtf8();
    QByteArray u = usuario.toUtf8();
    QByteArray p = password.toUtf8();
    QByteArray b = base.toUtf8();

    // Opciones de conexión cifrada (TLS) configurables en config.ini
    bool usarTls = (leerConfig("tls", "DB_TLS", "1") == "1");
    bool verificarCert = (leerConfig("tls_verificar", "DB_TLS_VERIFICAR", "0") == "1");
    QByteArray archivoCA = leerConfig("tls_ca", "DB_TLS_CA", "").toUtf8();

    const int INTENTOS = 2;
    for (int intento = 1; intento <= INTENTOS; intento++) {
        conexion = mysql_init(nullptr);
        if (conexion == nullptr) {
            detalleError = "No se pudo inicializar el conector de MariaDB.";
            return false;
        }

        unsigned int segundos = 30;
        mysql_options(conexion, MYSQL_OPT_CONNECT_TIMEOUT, &segundos);
        mysql_options(conexion, MYSQL_OPT_READ_TIMEOUT, &segundos);
        mysql_options(conexion, MYSQL_OPT_WRITE_TIMEOUT, &segundos);

        // Si se corta la conexión, se reconecta sola (conservando utf8mb4)
        bool reconectar = true;
        mysql_options(conexion, MYSQL_OPT_RECONNECT, &reconectar);
        mysql_options(conexion, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        if (usarTls) {
            bool exigir = true;
            mysql_options(conexion, MYSQL_OPT_SSL_ENFORCE, &exigir);
            mysql_options(conexion, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verificarCert);
            if (!archivoCA.isEmpty())
                mysql_options(conexion, MYSQL_OPT_SSL_CA, archivoCA.constData());
        }

        if (mysql_real_connect(conexion, h.constData(), u.constData(), p.constData(),
                               b.constData(), puerto, nullptr, 0)) {
            return true;
        }

        detalleError = errorBD();
        qDebug() << "Intento" << intento << "de" << INTENTOS << "falló:" << detalleError;
        mysql_close(conexion);
        conexion = nullptr;
    }
    return false;
}

// =============================================================================
// 3. CONTRASEÑAS (SHA-256 con sal)
// No se guarda la contraseña: se guarda su "hash" mezclado con una "sal"
// aleatoria distinta para cada usuario.
// =============================================================================
QString generarSalt() {
    QByteArray bytes;
    for (int i = 0; i < 16; i++)
        bytes.append(char(QRandomGenerator::system()->bounded(256)));
    return QString::fromLatin1(bytes.toHex());
}

QString hashPassword(const QString &salt, const QString &password) {
    QByteArray hash = QCryptographicHash::hash((salt + password).toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

// Convierte a hash las contraseñas que todavía estén en texto plano
void migrarPasswords() {
    QVector<QStringList> filas = consultar("SELECT id, password FROM usuarios WHERE salt IS NULL OR salt = ''");
    for (int i = 0; i < filas.size(); i++) {
        QString id = filas[i][0];
        QString passwordViejo = filas[i][1];
        QString salt = generarSalt();
        QString sql = "UPDATE usuarios SET salt='" + salt + "', password='" +
                      hashPassword(salt, passwordViejo) + "' WHERE id=" + id;
        ejecutar(sql);
    }
}

// =============================================================================
// 4. FUNCIONES DE AYUDA PARA LA INTERFAZ
// Aquí están los "new" y los punteros que Qt necesita para crear layouts,
// celdas de tabla y tarjetas de producto. Las pantallas solo llaman a estas
// funciones.
// =============================================================================
QString dinero(double valor) {
    return "$" + QString::number(valor, 'f', 2);
}

void aviso(QWidget *padre, const QString &mensaje) {
    QMessageBox::warning(padre, "Advertencia", mensaje);
}

void info(QWidget *padre, const QString &mensaje) {
    QMessageBox::information(padre, "Éxito", mensaje);
}

void falla(QWidget *padre, const QString &mensaje) {
    QMessageBox::critical(padre, "Error", mensaje);
}

bool confirmar(QWidget *padre, const QString &mensaje) {
    return QMessageBox::question(padre, "Confirmar", mensaje) == QMessageBox::Yes;
}

// Carga el archivo de estilos estilo_claro.qss
void cargarEstilo(QApplication &app) {
    QStringList carpetas;
    carpetas << QCoreApplication::applicationDirPath() << QDir::currentPath() << ":";
    for (int i = 0; i < carpetas.size(); i++) {
        QFile archivo(carpetas[i] + "/estilo_claro.qss");
        if (archivo.open(QIODevice::ReadOnly)) {
            app.setStyleSheet(QString::fromUtf8(archivo.readAll()));
            return;
        }
    }
    qWarning() << "No se encontró estilo_claro.qss, se usa el estilo por defecto.";
}

// ---------- Etiquetas y botones ----------
void configurarTitulo(QLabel &etiqueta, const QString &texto) {
    etiqueta.setText(texto);
    etiqueta.setObjectName("titulo");     // el estilo viene de estilo_claro.qss
}

// Pone el texto del botón y, si se indica, su estilo ("exito", "aviso", "peligro", "primario")
void configurarBoton(QPushButton &boton, const QString &texto, const QString &estilo = "") {
    boton.setText(texto);
    if (!estilo.isEmpty())
        boton.setProperty("estilo", estilo);
}

// ---------- Layouts ----------
// Cada función crea un layout, lo coloca dentro de su "padre" y lo devuelve.
// El padre se encarga de borrarlo solo.

// Layout principal de una ventana o contenedor
QVBoxLayout &crearLayoutVertical(QWidget &contenedor) {
    return *new QVBoxLayout(&contenedor);
}

QHBoxLayout &crearLayoutHorizontal(QWidget &contenedor) {
    return *new QHBoxLayout(&contenedor);
}

QFormLayout &crearLayoutFormulario(QWidget &contenedor) {
    return *new QFormLayout(&contenedor);
}

// Fila horizontal dentro de otro layout (estiramiento = cuánto espacio sobrante recibe)
QHBoxLayout &agregarFila(QBoxLayout &padre, int estiramiento = 0) {
    QHBoxLayout *fila = new QHBoxLayout();
    padre.addLayout(fila, estiramiento);
    return *fila;
}

// Columna vertical dentro de otro layout
QVBoxLayout &agregarColumna(QBoxLayout &padre, int estiramiento = 0) {
    QVBoxLayout *columna = new QVBoxLayout();
    padre.addLayout(columna, estiramiento);
    return *columna;
}

// Fila horizontal dentro de un formulario (con etiqueta opcional a la izquierda)
QHBoxLayout &agregarFilaAlFormulario(QFormLayout &formulario, const QString &etiqueta = "") {
    QHBoxLayout *fila = new QHBoxLayout();
    if (etiqueta.isEmpty())
        formulario.addRow(fila);
    else
        formulario.addRow(etiqueta, fila);
    return *fila;
}

// Agrega un texto fijo (por ejemplo "Cliente:") a un layout
void agregarTexto(QBoxLayout &layout, const QString &texto) {
    layout.addWidget(new QLabel(texto));
}

// ---------- Tablas ----------
// Deja la tabla lista: encabezados, selección por fila, solo lectura
void configurarTabla(QTableWidget &tabla, const QStringList &encabezados) {
    tabla.setColumnCount(encabezados.size());
    tabla.setHorizontalHeaderLabels(encabezados);
    tabla.setSelectionBehavior(QAbstractItemView::SelectRows);
    tabla.setSelectionMode(QAbstractItemView::SingleSelection);
    tabla.setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabla.horizontalHeader()->setStretchLastSection(true);
    tabla.verticalHeader()->setVisible(false);
    tabla.setAlternatingRowColors(true);
}

void llenarTabla(QTableWidget &tabla, const QVector<QStringList> &filas) {
    tabla.clearSelection();
    tabla.setRowCount(filas.size());
    for (int fila = 0; fila < filas.size(); fila++) {
        for (int col = 0; col < filas[fila].size(); col++) {
            tabla.setItem(fila, col, new QTableWidgetItem(filas[fila][col]));
        }
    }
    tabla.resizeColumnsToContents();
}

// Texto de una celda de la tabla ("" si no existe)
QString textoCelda(QTableWidget &tabla, int fila, int col) {
    QTableWidgetItem *celda = tabla.item(fila, col);
    if (celda == nullptr)
        return "";
    return celda->text();
}

// Número de la fila seleccionada, o -1 si no hay ninguna
int filaSeleccionada(QTableWidget &tabla) {
    QList<QTableWidgetItem *> elegidas = tabla.selectedItems();
    if (elegidas.isEmpty())
        return -1;
    return elegidas[0]->row();
}

// Pinta de un color todas las celdas de una fila
void pintarFila(QTableWidget &tabla, int fila, const QColor &fondo, const QColor &texto) {
    for (int col = 0; col < tabla.columnCount(); col++) {
        QTableWidgetItem *celda = tabla.item(fila, col);
        if (celda != nullptr) {
            celda->setBackground(fondo);
            celda->setForeground(texto);
        }
    }
}

// =============================================================================
// 5. IMÁGENES DE PRODUCTOS
// Se guardan en una carpeta de la aplicación; en la BD solo va el nombre del archivo
// =============================================================================
QString carpetaImagenes() {
    QString carpeta = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/imagenes";
    QDir().mkpath(carpeta);
    return carpeta;
}

// Devuelve la imagen del producto; si no tiene, un cuadro de color con su inicial
QPixmap imagenProducto(const QString &archivo, const QString &nombre, const QString &categoria, int tam = 128) {
    QPixmap lienzo(tam, tam);
    lienzo.fill(Qt::transparent);
    QPainter pintor(&lienzo);
    pintor.setRenderHint(QPainter::Antialiasing);

    QPixmap imagen;
    if (!archivo.isEmpty() && imagen.load(carpetaImagenes() + "/" + archivo)) {
        // Tiene imagen: se dibuja centrada
        QPixmap escalada = imagen.scaled(tam, tam, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pintor.drawPixmap((tam - escalada.width()) / 2, (tam - escalada.height()) / 2, escalada);
    } else {
        // No tiene imagen: cuadro de un color distinto por categoría + inicial
        pintor.setPen(Qt::NoPen);
        pintor.setBrush(QColor::fromHsl(int(qHash(categoria) % 360), 140, 150));
        pintor.drawRoundedRect(QRectF(0, 0, tam, tam), tam * 0.14, tam * 0.14);

        pintor.setPen(Qt::white);
        QFont fuente = pintor.font();
        fuente.setPixelSize(tam / 2);
        fuente.setBold(true);
        pintor.setFont(fuente);

        QString inicial = "?";
        if (!nombre.trimmed().isEmpty())
            inicial = nombre.trimmed().left(1).toUpper();
        pintor.drawText(lienzo.rect(), Qt::AlignCenter, inicial);
    }
    pintor.end();
    return lienzo;
}

// Agrega una tarjeta de producto (imagen + nombre + precio) a la lista del catálogo
void agregarProductoALista(QListWidget &lista, const QString &nombre, const QString &categoria,
                           const QString &imagen, double precio, int stock) {
    QString texto = nombre + "\n";
    if (stock > 0)
        texto += dinero(precio);
    else
        texto += "Agotado";

    QListWidgetItem *tarjeta = new QListWidgetItem(QIcon(imagenProducto(imagen, nombre, categoria)), texto);
    tarjeta->setSizeHint(QSize(142, 174));
    tarjeta->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    tarjeta->setToolTip(nombre + "\nExistencia: " + QString::number(stock));
    if (stock <= 0)
        tarjeta->setFlags(tarjeta->flags() & ~Qt::ItemIsEnabled);   // agotado: no se puede elegir
    lista.addItem(tarjeta);
}

// =============================================================================
// 6. TEXTO PARA REPORTES Y TICKETS
// =============================================================================

// Rellena un texto con espacios hasta cierto ancho, para que las columnas
// queden alineadas en el archivo .txt. Deja 2 espacios al final como separación.
QString textoColumna(const QString &texto, int ancho, bool alinearDerecha = false) {
    if (alinearDerecha)
        return texto.rightJustified(ancho, ' ', true) + "  ";
    return texto.leftJustified(ancho, ' ', true) + "  ";
}

// Guarda un texto en un archivo .txt elegido por el usuario
void exportarTxt(QWidget *padre, const QString &nombreBase, const QString &contenido) {
    QString carpeta = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString fecha = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString sugerido = carpeta + "/" + nombreBase + "_" + fecha + ".txt";

    QString ruta = QFileDialog::getSaveFileName(padre, "Guardar como", sugerido, "Archivos de texto (*.txt)");
    if (ruta.isEmpty())
        return;     // el usuario canceló

    QFile archivo(ruta);
    if (!archivo.open(QIODevice::WriteOnly | QIODevice::Text)) {
        falla(padre, "No se pudo escribir el archivo:\n" + ruta);
        return;
    }
    archivo.write(contenido.toUtf8());
    archivo.close();
    info(padre, "Archivo guardado en:\n" + ruta);
}

// Ventana que muestra el ticket después de cobrar, con opción de guardarlo
class VentanaTicket : public QDialog {
    QTextEdit visor;
    QPushButton btnGuardar;
    QPushButton btnCerrar;
    QString nombreArchivo;

public:
    VentanaTicket(QWidget *padre, const QString &texto, const QString &nombre) : QDialog(padre) {
        nombreArchivo = nombre;
        setWindowTitle("Ticket de venta");
        resize(520, 560);

        visor.setReadOnly(true);
        visor.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        visor.setPlainText(texto);
        configurarBoton(btnGuardar, "Guardar como .txt");
        configurarBoton(btnCerrar, "Cerrar");

        QVBoxLayout &raiz = crearLayoutVertical(*this);
        raiz.addWidget(&visor);
        QHBoxLayout &botones = agregarFila(raiz);
        botones.addWidget(&btnGuardar);
        botones.addWidget(&btnCerrar);

        // ----- Conexiones -----
        connect(&btnGuardar, &QPushButton::clicked, this, &VentanaTicket::guardarTicket);
        connect(&btnCerrar, &QPushButton::clicked, this, &QDialog::accept);
    }

private:
    // Se llama desde: el botón "Guardar como .txt"
    void guardarTicket() {
        exportarTxt(this, nombreArchivo, visor.toPlainText());
    }
};

// =============================================================================
// 7. PÁGINA DE VENTAS
// Catálogo con imágenes a la izquierda; carrito y cobro a la derecha.
// =============================================================================
struct Producto {
    int id;
    QString nombre;
    double precio;
    int stock;
    QString categoria;
};

struct ItemCarrito {
    int id;
    QString nombre;
    double precio;
    int cantidad;
};

class PaginaVentas : public QWidget {
    // ----- Lado izquierdo: catálogo -----
    QLabel titulo;
    QLineEdit buscar;
    QComboBox cmbCategoria;
    QSpinBox spCantidad;
    QListWidget listaProductos;
    // ----- Lado derecho: carrito y cobro -----
    QComboBox cmbCliente;
    QTableWidget tabla;
    QPushButton btnMenos;
    QPushButton btnMas;
    QPushButton btnQuitar;
    QPushButton btnVaciar;
    QComboBox cmbPago;
    QDoubleSpinBox spRecibido;
    QLabel lblCambio;
    QLabel lblTotal;
    QPushButton btnCobrar;
    // ----- Datos -----
    QVector<Producto> catalogo;      // productos, en el mismo orden que la lista de tarjetas
    QVector<ItemCarrito> carrito;

public:
    PaginaVentas() {
        crearInterfaz();
        conectarEventos();
    }

    // Se llama cada vez que se abre esta pantalla desde el menú
    void refrescar() {
        cargarClientes();
        cargarProductos();
        filtrar();
        mostrarCarrito();
    }

private:
    // ---------- Interfaz ----------
    void crearInterfaz() {
        QVBoxLayout &raiz = crearLayoutVertical(*this);
        configurarTitulo(titulo, "Nueva venta");
        raiz.addWidget(&titulo);

        QHBoxLayout &cuerpo = agregarFila(raiz, 1);

        // --- Lado izquierdo: catálogo ---
        QVBoxLayout &izquierda = agregarColumna(cuerpo, 3);

        buscar.setPlaceholderText("Buscar producto...");
        buscar.setClearButtonEnabled(true);
        cmbCategoria.setMinimumWidth(170);
        spCantidad.setRange(1, 100000);
        spCantidad.setPrefix("Cant. ");

        QHBoxLayout &filtros = agregarFila(izquierda);
        filtros.addWidget(&buscar, 1);
        filtros.addWidget(&cmbCategoria);
        filtros.addWidget(&spCantidad);

        listaProductos.setObjectName("productos");
        listaProductos.setViewMode(QListView::IconMode);
        listaProductos.setResizeMode(QListView::Adjust);
        listaProductos.setMovement(QListView::Static);
        listaProductos.setIconSize(QSize(TAM_ICONO, TAM_ICONO));
        listaProductos.setGridSize(QSize(152, 184));
        listaProductos.setWordWrap(true);
        listaProductos.setSelectionMode(QAbstractItemView::NoSelection);
        listaProductos.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listaProductos.setCursor(Qt::PointingHandCursor);
        izquierda.addWidget(&listaProductos, 1);

        // --- Lado derecho: carrito y cobro ---
        QVBoxLayout &derecha = agregarColumna(cuerpo, 2);

        QHBoxLayout &filaCliente = agregarFila(derecha);
        agregarTexto(filaCliente, "Cliente:");
        filaCliente.addWidget(&cmbCliente, 1);

        configurarTabla(tabla, QStringList() << "Producto" << "Precio" << "Cant." << "Subtotal");
        tabla.setMinimumWidth(360);
        derecha.addWidget(&tabla, 1);

        configurarBoton(btnMenos, "−");
        configurarBoton(btnMas, "+");
        configurarBoton(btnQuitar, "Quitar");
        configurarBoton(btnVaciar, "Vaciar");
        QHBoxLayout &filaCarrito = agregarFila(derecha);
        filaCarrito.addWidget(&btnMenos);
        filaCarrito.addWidget(&btnMas);
        filaCarrito.addWidget(&btnQuitar);
        filaCarrito.addWidget(&btnVaciar);

        cmbPago.addItem("Efectivo");
        cmbPago.addItem("Tarjeta");
        cmbPago.addItem("Transferencia");
        QHBoxLayout &filaPago = agregarFila(derecha);
        agregarTexto(filaPago, "Pago:");
        filaPago.addWidget(&cmbPago, 1);

        spRecibido.setRange(0, 10000000);
        spRecibido.setDecimals(2);
        spRecibido.setPrefix("$");
        QHBoxLayout &filaRecibido = agregarFila(derecha);
        agregarTexto(filaRecibido, "Recibido:");
        filaRecibido.addWidget(&spRecibido, 1);
        filaRecibido.addWidget(&lblCambio);

        lblTotal.setText("Total: $0.00");
        lblTotal.setObjectName("total");
        lblTotal.setAlignment(Qt::AlignCenter);
        derecha.addWidget(&lblTotal);

        configurarBoton(btnCobrar, "COBRAR", "exito");
        btnCobrar.setObjectName("cobrar");
        derecha.addWidget(&btnCobrar);
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&listaProductos, &QListWidget::clicked, this, &PaginaVentas::clicEnProducto);
        connect(&buscar, &QLineEdit::textChanged, this, &PaginaVentas::filtrar);
        connect(&cmbCategoria, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaginaVentas::filtrar);

        connect(&btnMas, &QPushButton::clicked, this, &PaginaVentas::sumarUno);
        connect(&btnMenos, &QPushButton::clicked, this, &PaginaVentas::restarUno);
        connect(&btnQuitar, &QPushButton::clicked, this, &PaginaVentas::quitarDelCarrito);
        connect(&btnVaciar, &QPushButton::clicked, this, &PaginaVentas::vaciarCarrito);

        connect(&cmbPago, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaginaVentas::actualizarCambio);
        connect(&spRecibido, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PaginaVentas::actualizarCambio);
        connect(&btnCobrar, &QPushButton::clicked, this, &PaginaVentas::cobrar);
    }

    // ---------- Cargar datos ----------
    void cargarClientes() {
        int clienteActual = cmbCliente.currentData().toInt();
        cmbCliente.clear();
        cmbCliente.addItem("Público general", 0);

        QVector<QStringList> clientes = consultar("SELECT id, nombre FROM clientes ORDER BY nombre");
        for (int i = 0; i < clientes.size(); i++)
            cmbCliente.addItem(clientes[i][1], clientes[i][0].toInt());

        int pos = cmbCliente.findData(clienteActual);
        if (pos >= 0)
            cmbCliente.setCurrentIndex(pos);
        else
            cmbCliente.setCurrentIndex(0);
    }

    void cargarProductos() {
        QString categoriaActual = cmbCategoria.currentData().toString();

        QVector<QStringList> filas = consultar(
            "SELECT id, nombre_articulo, existencia, precio, "
            "COALESCE(NULLIF(categoria,''),'Sin categoría'), COALESCE(imagen,'') "
            "FROM productos ORDER BY nombre_articulo");

        catalogo.clear();
        listaProductos.clear();
        QStringList categorias;

        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];

            Producto p;
            p.id = fila[0].toInt();
            p.nombre = fila[1];
            p.stock = fila[2].toInt();
            p.precio = fila[3].toDouble();
            p.categoria = fila[4];
            QString imagen = fila[5];

            catalogo.push_back(p);
            if (!categorias.contains(p.categoria))
                categorias << p.categoria;

            agregarProductoALista(listaProductos, p.nombre, p.categoria, imagen, p.precio, p.stock);
        }

        // Llenar el filtro de categorías conservando la elección anterior
        categorias.sort(Qt::CaseInsensitive);
        cmbCategoria.blockSignals(true);
        cmbCategoria.clear();
        cmbCategoria.addItem("Todas las categorías", QString());
        for (int i = 0; i < categorias.size(); i++)
            cmbCategoria.addItem(categorias[i], categorias[i]);
        int pos = cmbCategoria.findData(categoriaActual);
        if (pos >= 0)
            cmbCategoria.setCurrentIndex(pos);
        else
            cmbCategoria.setCurrentIndex(0);
        cmbCategoria.blockSignals(false);
    }

    // Oculta los productos que no coinciden con la búsqueda o la categoría
    // Se llama desde: el campo de búsqueda, el filtro de categoría y refrescar()
    void filtrar() {
        QString texto = buscar.text().trimmed();
        QString categoria = cmbCategoria.currentData().toString();

        for (int i = 0; i < catalogo.size(); i++) {
            bool coincideTexto = texto.isEmpty() || catalogo[i].nombre.contains(texto, Qt::CaseInsensitive);
            bool coincideCategoria = categoria.isEmpty() || catalogo[i].categoria == categoria;
            listaProductos.setRowHidden(i, !(coincideTexto && coincideCategoria));
        }
    }

    // ---------- Carrito ----------
    double calcularTotal() {
        double suma = 0;
        for (int i = 0; i < carrito.size(); i++)
            suma += carrito[i].precio * carrito[i].cantidad;
        return qRound64(suma * 100) / 100.0;
    }

    int cantidadEnCarrito(int idProducto) {
        for (int i = 0; i < carrito.size(); i++) {
            if (carrito[i].id == idProducto)
                return carrito[i].cantidad;
        }
        return 0;
    }

    // Existencia actual de un producto (según el catálogo cargado)
    int existenciaDe(int idProducto) {
        for (int i = 0; i < catalogo.size(); i++) {
            if (catalogo[i].id == idProducto)
                return catalogo[i].stock;
        }
        return 0;
    }

    // Un clic en un producto del catálogo lo agrega al carrito
    // Se llama desde: un clic en la lista de productos
    void clicEnProducto(const QModelIndex &indice) {
        int fila = indice.row();
        if (fila < 0 || fila >= catalogo.size())
            return;

        Producto producto = catalogo[fila];
        if (producto.stock <= 0)
            return;                      // agotado

        int cantidad = spCantidad.value();
        if (cantidadEnCarrito(producto.id) + cantidad > producto.stock) {
            aviso(this, "Existencia insuficiente de '" + producto.nombre + "'. Disponible: " +
                            QString::number(producto.stock));
            return;
        }

        // Si ya estaba en el carrito, solo sumamos la cantidad
        bool encontrado = false;
        for (int i = 0; i < carrito.size(); i++) {
            if (carrito[i].id == producto.id) {
                carrito[i].cantidad += cantidad;
                encontrado = true;
                break;
            }
        }
        if (!encontrado) {
            ItemCarrito nuevo;
            nuevo.id = producto.id;
            nuevo.nombre = producto.nombre;
            nuevo.precio = producto.precio;
            nuevo.cantidad = cantidad;
            carrito.push_back(nuevo);
        }

        spCantidad.setValue(1);
        mostrarCarrito();
        tabla.scrollToBottom();
    }

    // Se llama desde: el botón "+"
    void sumarUno() {
        cambiarCantidad(+1);
    }

    // Se llama desde: el botón "−"
    void restarUno() {
        cambiarCantidad(-1);
    }

    // Suma o resta unidades a la fila seleccionada del carrito
    // Se llama desde: sumarUno() y restarUno()
    void cambiarCantidad(int cambio) {
        int fila = filaSeleccionada(tabla);
        if (fila < 0 || fila >= carrito.size()) {
            aviso(this, "Selecciona una fila del carrito.");
            return;
        }

        int nueva = carrito[fila].cantidad + cambio;
        int disponible = existenciaDe(carrito[fila].id);

        if (nueva <= 0) {
            carrito.removeAt(fila);
        } else if (nueva > disponible) {
            aviso(this, "Existencia insuficiente. Disponible: " + QString::number(disponible));
            return;
        } else {
            carrito[fila].cantidad = nueva;
        }

        mostrarCarrito();
        if (tabla.rowCount() > 0)
            tabla.selectRow(qMin(fila, tabla.rowCount() - 1));
    }

    // Se llama desde: el botón "Quitar"
    void quitarDelCarrito() {
        int fila = filaSeleccionada(tabla);
        if (fila < 0) {
            aviso(this, "Selecciona una fila del carrito.");
            return;
        }
        carrito.removeAt(fila);
        mostrarCarrito();
    }

    // Se llama desde: el botón "Vaciar"
    void vaciarCarrito() {
        carrito.clear();
        mostrarCarrito();
    }

    // Dibuja el carrito en la tabla y actualiza el total
    void mostrarCarrito() {
        QVector<QStringList> filas;
        for (int i = 0; i < carrito.size(); i++) {
            QStringList fila;
            fila << carrito[i].nombre;
            fila << dinero(carrito[i].precio);
            fila << QString::number(carrito[i].cantidad);
            fila << dinero(carrito[i].precio * carrito[i].cantidad);
            filas.push_back(fila);
        }
        llenarTabla(tabla, filas);
        lblTotal.setText("Total: " + dinero(calcularTotal()));
        actualizarCambio();
    }

    // Calcula el cambio (solo cuando se paga en efectivo)
    // Se llama desde: el método de pago, el monto recibido y mostrarCarrito()
    void actualizarCambio() {
        bool efectivo = (cmbPago.currentText() == "Efectivo");
        spRecibido.setEnabled(efectivo);
        if (efectivo) {
            double cambio = spRecibido.value() - calcularTotal();
            if (cambio < 0)
                cambio = 0;
            lblCambio.setText("Cambio: " + dinero(cambio));
        } else {
            lblCambio.setText("");
        }
    }

    // ---------- Cobro ----------
    // Se llama desde: el botón "COBRAR"
    void cobrar() {
        if (carrito.isEmpty()) {
            aviso(this, "El carrito está vacío.");
            return;
        }

        double total = calcularTotal();
        QString metodo = cmbPago.currentText();
        bool efectivo = (metodo == "Efectivo");

        if (efectivo && spRecibido.value() + 0.001 < total) {
            aviso(this, "El monto recibido es menor al total.");
            return;
        }

        qulonglong folio = 0;
        QString error = guardarVenta(metodo, total, folio);
        if (!error.isEmpty()) {
            falla(this, "No se pudo completar la venta:\n" + error);
            refrescar();
            return;
        }

        // La venta se guardó: armamos el ticket antes de vaciar el carrito
        QString ticket = armarTicket(folio, metodo, total, efectivo);

        carrito.clear();
        spRecibido.setValue(0);
        refrescar();

        VentanaTicket ventana(this, ticket, "ticket_" + QString::number(folio));
        ventana.exec();
    }

    // Guarda la venta en la base de datos.
    // Si algo falla se cancela todo (ROLLBACK). Devuelve "" si todo salió bien.
    // Se llama desde: cobrar()
    QString guardarVenta(const QString &metodo, double total, qulonglong &folio) {
        int clienteId = cmbCliente.currentData().toInt();
        QString cliente = "NULL";
        if (clienteId != 0)
            cliente = QString::number(clienteId);

        ejecutar("START TRANSACTION");
        QString error;

        // 1) Registrar la venta
        QString sqlVenta = "INSERT INTO ventas (cliente_id, usuario_id, metodo_pago, total) VALUES (" +
                           cliente + ", " + QString::number(sesionId) + ", '" + esc(metodo) + "', " +
                           QString::number(total, 'f', 2) + ")";
        if (!ejecutar(sqlVenta))
            error = errorBD();
        folio = ultimoIdInsertado();

        // 2) Descontar existencia y guardar el detalle de cada producto
        for (int i = 0; error.isEmpty() && i < carrito.size(); i++) {
            ItemCarrito item = carrito[i];

            // Solo descuenta si todavía alcanza la existencia
            QString sqlStock = "UPDATE productos SET existencia = existencia - " + QString::number(item.cantidad) +
                               " WHERE id = " + QString::number(item.id) +
                               " AND existencia >= " + QString::number(item.cantidad);
            if (!ejecutar(sqlStock)) {
                error = errorBD();
            } else if (filasAfectadas() != 1) {
                error = "Existencia insuficiente de '" + item.nombre + "'.";
            } else {
                QString sqlDetalle =
                    "INSERT INTO detalle_venta (venta_id, producto_id, nombre_articulo, cantidad, "
                    "precio_unitario, subtotal) VALUES (" +
                    QString::number(folio) + ", " + QString::number(item.id) + ", '" + esc(item.nombre) + "', " +
                    QString::number(item.cantidad) + ", " + QString::number(item.precio, 'f', 2) + ", " +
                    QString::number(item.precio * item.cantidad, 'f', 2) + ")";
                if (!ejecutar(sqlDetalle))
                    error = errorBD();
            }
        }

        // 3) Confirmar o cancelar
        if (error.isEmpty() && !ejecutar("COMMIT"))
            error = errorBD();
        if (!error.isEmpty())
            ejecutar("ROLLBACK");

        return error;
    }

    // Se llama desde: cobrar()
    QString armarTicket(qulonglong folio, const QString &metodo, double total, bool efectivo) {
        QString t;
        t += "      " + NOMBRE_NEGOCIO.toUpper() + "\n";
        t += "Folio:   " + QString::number(folio) + "\n";
        t += "Fecha:   " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") + "\n";
        t += "Cajero:  " + sesionUsuario + "\n";
        t += "Cliente: " + cmbCliente.currentText() + "\n";
        t += QString("-").repeated(40) + "\n";
        for (int i = 0; i < carrito.size(); i++) {
            t += carrito[i].nombre.leftJustified(24, ' ', true);
            t += QString("x%1").arg(carrito[i].cantidad).rightJustified(5);
            t += dinero(carrito[i].precio * carrito[i].cantidad).rightJustified(11) + "\n";
        }
        t += QString("-").repeated(40) + "\n";
        t += QString("TOTAL:").leftJustified(29) + dinero(total).rightJustified(11) + "\n";
        t += "Pago: " + metodo + "\n";
        if (efectivo) {
            t += "Recibido: " + dinero(spRecibido.value()) + "\n";
            t += "Cambio:   " + dinero(spRecibido.value() - total) + "\n";
        }
        t += "\n      ¡Gracias por su compra!\n";
        return t;
    }
};

// =============================================================================
// 8. PÁGINA DE INVENTARIO (el vendedor solo puede consultar)
// =============================================================================
class PaginaInventario : public QWidget {
    QLabel titulo;
    QLineEdit buscar;
    QTableWidget tabla;

    // El formulario es un contenedor: se declara ANTES de los widgets que contiene
    QGroupBox formulario;
    QLineEdit entNombre;
    QLineEdit entCategoria;
    QSpinBox entStock;
    QDoubleSpinBox entPrecio;
    QComboBox cmbProv;
    QLabel lblPreview;
    QPushButton btnImagen;
    QPushButton btnQuitarImagen;
    QPushButton btnCrear;
    QPushButton btnMod;
    QPushButton btnDel;
    QPushButton btnLimpiar;

    QString imagenActual;   // nombre del archivo de imagen del producto
    int idSel = 0;          // id del producto seleccionado (0 = ninguno)

public:
    PaginaInventario() {
        crearInterfaz();
        conectarEventos();
    }

    // Se llama cada vez que se abre esta pantalla desde el menú
    void refrescar() {
        // Llenar la lista de proveedores
        cmbProv.clear();
        cmbProv.addItem("(Sin proveedor)", 0);
        QVector<QStringList> proveedores = consultar("SELECT id, nombre_empresa FROM proveedores ORDER BY nombre_empresa");
        for (int i = 0; i < proveedores.size(); i++)
            cmbProv.addItem(proveedores[i][1], proveedores[i][0].toInt());

        cargar();
    }

private:
    void crearInterfaz() {
        QVBoxLayout &raiz = crearLayoutVertical(*this);
        configurarTitulo(titulo, "Inventario");
        raiz.addWidget(&titulo);

        buscar.setPlaceholderText("Buscar por nombre o categoría...");
        raiz.addWidget(&buscar);

        configurarTabla(tabla, QStringList() << "ID" << "Artículo" << "Categoría" << "Existencia"
                                             << "Precio" << "Proveedor" << "ProvID" << "Imagen");
        tabla.setColumnHidden(6, true);   // columnas auxiliares ocultas
        tabla.setColumnHidden(7, true);
        raiz.addWidget(&tabla, 1);

        // --- Formulario ---
        formulario.setTitle("Datos del producto");
        QFormLayout &form = crearLayoutFormulario(formulario);

        entCategoria.setPlaceholderText("ej: Bebidas, Snacks, Desinfectantes");
        entStock.setRange(0, 1000000);
        entPrecio.setRange(0, 1000000);
        entPrecio.setDecimals(2);
        entPrecio.setPrefix("$");

        form.addRow("Nombre:", &entNombre);
        form.addRow("Categoría:", &entCategoria);
        form.addRow("Existencia:", &entStock);
        form.addRow("Precio:", &entPrecio);
        form.addRow("Proveedor:", &cmbProv);

        // Imagen del producto
        lblPreview.setObjectName("preview");
        lblPreview.setFixedSize(76, 76);
        lblPreview.setAlignment(Qt::AlignCenter);
        configurarBoton(btnImagen, "Elegir imagen...");
        configurarBoton(btnQuitarImagen, "Quitar imagen");
        QHBoxLayout &filaImagen = agregarFilaAlFormulario(form, "Imagen:");
        filaImagen.addWidget(&lblPreview);
        filaImagen.addWidget(&btnImagen);
        filaImagen.addWidget(&btnQuitarImagen);
        filaImagen.addStretch();

        // Botones
        configurarBoton(btnCrear, "Añadir", "exito");
        configurarBoton(btnMod, "Modificar", "aviso");
        configurarBoton(btnDel, "Eliminar", "peligro");
        configurarBoton(btnLimpiar, "Limpiar");
        QHBoxLayout &botones = agregarFilaAlFormulario(form);
        botones.addWidget(&btnCrear);
        botones.addWidget(&btnMod);
        botones.addWidget(&btnDel);
        botones.addWidget(&btnLimpiar);

        raiz.addWidget(&formulario);

        // El vendedor no puede modificar el inventario
        formulario.setVisible(esAdmin());
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&buscar, &QLineEdit::textChanged, this, &PaginaInventario::cargar);
        connect(&tabla, &QTableWidget::itemSelectionChanged, this, &PaginaInventario::seleccionarFila);
        connect(&entNombre, &QLineEdit::textChanged, this, &PaginaInventario::actualizarPreview);
        connect(&entCategoria, &QLineEdit::textChanged, this, &PaginaInventario::actualizarPreview);

        connect(&btnImagen, &QPushButton::clicked, this, &PaginaInventario::elegirImagen);
        connect(&btnQuitarImagen, &QPushButton::clicked, this, &PaginaInventario::quitarImagen);
        connect(&btnCrear, &QPushButton::clicked, this, &PaginaInventario::anadir);
        connect(&btnMod, &QPushButton::clicked, this, &PaginaInventario::modificar);
        connect(&btnDel, &QPushButton::clicked, this, &PaginaInventario::eliminar);
        connect(&btnLimpiar, &QPushButton::clicked, this, &PaginaInventario::limpiar);

        actualizarPreview();
    }

    // Llena la tabla con los productos (aplicando la búsqueda)
    // Se llama desde: el campo de búsqueda, refrescar() y después de guardar o eliminar
    void cargar() {
        QString b = esc(buscar.text().trimmed());
        QVector<QStringList> filas = consultar(
            "SELECT p.id, p.nombre_articulo, COALESCE(p.categoria,''), p.existencia, p.precio, "
            "COALESCE(pr.nombre_empresa,'—'), COALESCE(p.proveedor_id,0), COALESCE(p.imagen,'') "
            "FROM productos p LEFT JOIN proveedores pr ON pr.id = p.proveedor_id "
            "WHERE p.nombre_articulo LIKE '%" + b + "%' OR p.categoria LIKE '%" + b + "%' "
                                                    "ORDER BY p.nombre_articulo");

        // El precio se muestra con formato de dinero
        for (int i = 0; i < filas.size(); i++)
            filas[i][4] = dinero(filas[i][4].toDouble());
        llenarTabla(tabla, filas);

        // Pintar de rojo claro los productos con existencia baja
        for (int fila = 0; fila < tabla.rowCount(); fila++) {
            if (textoCelda(tabla, fila, 3).toInt() <= STOCK_BAJO)
                pintarFila(tabla, fila, QColor(255, 220, 220), Qt::black);
        }
    }

    // Al elegir una fila, se copian sus datos al formulario
    // Se llama desde: un cambio de selección en la tabla
    void seleccionarFila() {
        int fila = filaSeleccionada(tabla);
        if (fila < 0)
            return;

        idSel = textoCelda(tabla, fila, 0).toInt();
        entNombre.setText(textoCelda(tabla, fila, 1));
        entCategoria.setText(textoCelda(tabla, fila, 2));
        entStock.setValue(textoCelda(tabla, fila, 3).toInt());
        entPrecio.setValue(textoCelda(tabla, fila, 4).remove('$').toDouble());

        int pos = cmbProv.findData(textoCelda(tabla, fila, 6).toInt());
        if (pos >= 0)
            cmbProv.setCurrentIndex(pos);
        else
            cmbProv.setCurrentIndex(0);

        imagenActual = textoCelda(tabla, fila, 7);
        actualizarPreview();
    }

    // Se llama desde: los campos Nombre y Categoría, y cuando cambia la imagen
    void actualizarPreview() {
        QPixmap imagen = imagenProducto(imagenActual, entNombre.text(), entCategoria.text(), 128);
        lblPreview.setPixmap(imagen.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // Copia la imagen elegida (reducida) a la carpeta de la aplicación.
    // Se asocia al producto cuando se pulsa Añadir o Modificar.
    // Se llama desde: el botón "Elegir imagen..."
    void elegirImagen() {
        QString ruta = QFileDialog::getOpenFileName(
            this, "Elegir imagen del producto",
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            "Imágenes (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (ruta.isEmpty())
            return;

        QPixmap original;
        if (!original.load(ruta)) {
            falla(this, "No se pudo abrir la imagen seleccionada.");
            return;
        }

        QString archivo = QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
        QPixmap reducida = original.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!reducida.save(carpetaImagenes() + "/" + archivo, "PNG")) {
            falla(this, "No se pudo guardar la imagen en:\n" + carpetaImagenes());
            return;
        }
        imagenActual = archivo;
        actualizarPreview();
    }

    // Se llama desde: el botón "Quitar imagen"
    void quitarImagen() {
        imagenActual.clear();
        actualizarPreview();
    }

    // Se llama desde: el botón "Limpiar" y después de guardar o eliminar
    void limpiar() {
        idSel = 0;
        entNombre.clear();
        entCategoria.clear();
        entStock.setValue(0);
        entPrecio.setValue(0);
        cmbProv.setCurrentIndex(0);
        imagenActual.clear();
        actualizarPreview();
        tabla.clearSelection();
    }

    // Revisa los datos del formulario. Devuelve false (y avisa) si algo está mal
    // Se llama desde: anadir() y modificar()
    bool datosValidos() {
        if (entNombre.text().trimmed().isEmpty()) {
            aviso(this, "El nombre es obligatorio.");
            return false;
        }
        if (entPrecio.value() <= 0) {
            aviso(this, "El precio debe ser mayor a cero.");
            return false;
        }
        return true;
    }

    // Devuelve el id del proveedor elegido, o NULL si no eligió ninguno
    QString proveedorSql() {
        int provId = cmbProv.currentData().toInt();
        if (provId == 0)
            return "NULL";
        return QString::number(provId);
    }

    // Se llama desde: el botón "Añadir"
    void anadir() {
        if (!datosValidos())
            return;

        QString nombre = entNombre.text().trimmed();
        QString sql = "INSERT INTO productos (nombre_articulo, categoria, existencia, precio, proveedor_id, imagen) VALUES (" +
                      sqlTxt(nombre) + ", " + sqlTxt(entCategoria.text()) + ", " + QString::number(entStock.value()) + ", " +
                      QString::number(entPrecio.value(), 'f', 2) + ", " + proveedorSql() + ", " + sqlTxt(imagenActual) + ")";

        if (ejecutar(sql)) {
            info(this, "'" + nombre + "' agregado correctamente.");
            limpiar();
            cargar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    // Se llama desde: el botón "Modificar"
    void modificar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un producto de la tabla para modificarlo.");
            return;
        }
        if (!datosValidos())
            return;

        QString sql = "UPDATE productos SET nombre_articulo=" + sqlTxt(entNombre.text()) +
                      ", categoria=" + sqlTxt(entCategoria.text()) +
                      ", existencia=" + QString::number(entStock.value()) +
                      ", precio=" + QString::number(entPrecio.value(), 'f', 2) +
                      ", proveedor_id=" + proveedorSql() +
                      ", imagen=" + sqlTxt(imagenActual) +
                      " WHERE id=" + QString::number(idSel);

        if (ejecutar(sql)) {
            info(this, "Producto modificado.");
            limpiar();
            cargar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    // Se llama desde: el botón "Eliminar"
    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un producto de la tabla.");
            return;
        }
        if (!confirmar(this, "¿Eliminar '" + entNombre.text() + "'?"))
            return;

        if (ejecutar("DELETE FROM productos WHERE id = " + QString::number(idSel))) {
            info(this, "Producto eliminado.");
            limpiar();
            cargar();
        } else {
            falla(this, "No se pudo eliminar: " + errorBD());
        }
    }
};

// =============================================================================
// 9. PÁGINA DE CLIENTES
// =============================================================================
class PaginaClientes : public QWidget {
    QLabel titulo;
    QLineEdit buscar;
    QTableWidget tabla;

    // El formulario es un contenedor: se declara ANTES de los widgets que contiene
    QGroupBox formulario;
    QLineEdit entNombre;
    QLineEdit entTelefono;
    QLineEdit entEmail;
    QLineEdit entDireccion;
    QPushButton btnCrear;
    QPushButton btnMod;
    QPushButton btnDel;
    QPushButton btnLimpiar;

    int idSel = 0;          // id del cliente seleccionado (0 = ninguno)

public:
    PaginaClientes() {
        crearInterfaz();
        conectarEventos();
    }

    // Llena la tabla con los clientes (aplicando la búsqueda)
    // Se llama desde: el menú, el campo de búsqueda y después de guardar o eliminar
    void refrescar() {
        QString b = esc(buscar.text().trimmed());

        // El total comprado se calcula sumando las ventas de cada cliente
        QVector<QStringList> filas = consultar(
            "SELECT c.id, c.nombre, COALESCE(c.telefono,''), COALESCE(c.email,''), "
            "COALESCE(c.direccion,''), DATE(c.fecha_registro), COALESCE(SUM(v.total),0) "
            "FROM clientes c LEFT JOIN ventas v ON v.cliente_id = c.id "
            "WHERE c.nombre LIKE '%" + b + "%' OR c.telefono LIKE '%" + b + "%' OR c.email LIKE '%" + b + "%' "
                                                                                 "GROUP BY c.id ORDER BY c.nombre");

        for (int i = 0; i < filas.size(); i++)
            filas[i][6] = dinero(filas[i][6].toDouble());
        llenarTabla(tabla, filas);
    }

private:
    void crearInterfaz() {
        QVBoxLayout &raiz = crearLayoutVertical(*this);
        configurarTitulo(titulo, "Clientes");
        raiz.addWidget(&titulo);

        buscar.setPlaceholderText("Buscar por nombre, teléfono o correo...");
        raiz.addWidget(&buscar);

        configurarTabla(tabla, QStringList() << "ID" << "Nombre" << "Teléfono" << "Email"
                                             << "Dirección" << "Registro" << "Total comprado");
        raiz.addWidget(&tabla, 1);

        // --- Formulario ---
        formulario.setTitle("Datos del cliente");
        QFormLayout &form = crearLayoutFormulario(formulario);
        form.addRow("Nombre:", &entNombre);
        form.addRow("Teléfono:", &entTelefono);
        form.addRow("Email:", &entEmail);
        form.addRow("Dirección:", &entDireccion);

        configurarBoton(btnCrear, "Añadir", "exito");
        configurarBoton(btnMod, "Modificar", "aviso");
        configurarBoton(btnDel, "Eliminar", "peligro");
        configurarBoton(btnLimpiar, "Limpiar");
        QHBoxLayout &botones = agregarFilaAlFormulario(form);
        botones.addWidget(&btnCrear);
        botones.addWidget(&btnMod);
        botones.addWidget(&btnDel);
        botones.addWidget(&btnLimpiar);

        raiz.addWidget(&formulario);

        // Solo el administrador puede eliminar clientes
        btnDel.setVisible(esAdmin());
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&buscar, &QLineEdit::textChanged, this, &PaginaClientes::refrescar);
        connect(&tabla, &QTableWidget::itemSelectionChanged, this, &PaginaClientes::seleccionarFila);

        connect(&btnCrear, &QPushButton::clicked, this, &PaginaClientes::anadir);
        connect(&btnMod, &QPushButton::clicked, this, &PaginaClientes::modificar);
        connect(&btnDel, &QPushButton::clicked, this, &PaginaClientes::eliminar);
        connect(&btnLimpiar, &QPushButton::clicked, this, &PaginaClientes::limpiar);
    }

    // Al elegir una fila, se copian sus datos al formulario
    // Se llama desde: un cambio de selección en la tabla
    void seleccionarFila() {
        int fila = filaSeleccionada(tabla);
        if (fila < 0)
            return;

        idSel = textoCelda(tabla, fila, 0).toInt();
        entNombre.setText(textoCelda(tabla, fila, 1));
        entTelefono.setText(textoCelda(tabla, fila, 2));
        entEmail.setText(textoCelda(tabla, fila, 3));
        entDireccion.setText(textoCelda(tabla, fila, 4));
    }

    // Se llama desde: el botón "Limpiar" y después de guardar o eliminar
    void limpiar() {
        idSel = 0;
        entNombre.clear();
        entTelefono.clear();
        entEmail.clear();
        entDireccion.clear();
        tabla.clearSelection();
    }

    // Se llama desde: el botón "Añadir"
    void anadir() {
        QString nombre = entNombre.text().trimmed();
        if (nombre.isEmpty()) {
            aviso(this, "El nombre del cliente es obligatorio.");
            return;
        }

        QString sql = "INSERT INTO clientes (nombre, telefono, email, direccion) VALUES (" +
                      sqlTxt(nombre) + ", " + sqlTxt(entTelefono.text()) + ", " +
                      sqlTxt(entEmail.text()) + ", " + sqlTxt(entDireccion.text()) + ")";

        if (ejecutar(sql)) {
            info(this, "Cliente registrado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    // Se llama desde: el botón "Modificar"
    void modificar() {
        QString nombre = entNombre.text().trimmed();
        if (nombre.isEmpty()) {
            aviso(this, "El nombre del cliente es obligatorio.");
            return;
        }
        if (idSel == 0) {
            aviso(this, "Selecciona un cliente de la tabla para modificarlo.");
            return;
        }

        QString sql = "UPDATE clientes SET nombre=" + sqlTxt(nombre) +
                      ", telefono=" + sqlTxt(entTelefono.text()) +
                      ", email=" + sqlTxt(entEmail.text()) +
                      ", direccion=" + sqlTxt(entDireccion.text()) +
                      " WHERE id=" + QString::number(idSel);

        if (ejecutar(sql)) {
            info(this, "Cliente actualizado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    // Se llama desde: el botón "Eliminar"
    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un cliente de la tabla.");
            return;
        }
        QString pregunta = "¿Eliminar al cliente '" + entNombre.text() +
                           "'?\nSus ventas se conservarán como 'Público general'.";
        if (!confirmar(this, pregunta))
            return;

        if (ejecutar("DELETE FROM clientes WHERE id = " + QString::number(idSel))) {
            info(this, "Cliente eliminado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo eliminar: " + errorBD());
        }
    }
};

// =============================================================================
// 10. PÁGINA DE USUARIOS (solo administrador)
// =============================================================================
class PaginaUsuarios : public QWidget {
    QLabel titulo;
    QTableWidget tabla;

    // El formulario es un contenedor: se declara ANTES de los widgets que contiene
    QGroupBox formulario;
    QLineEdit entUsuario;
    QLineEdit entEmail;
    QLineEdit entPassword;
    QComboBox cmbRol;
    QCheckBox chkActivo;
    QPushButton btnCrear;
    QPushButton btnMod;
    QPushButton btnDel;
    QPushButton btnLimpiar;

    int idSel = 0;          // id del usuario seleccionado (0 = ninguno)

public:
    PaginaUsuarios() {
        crearInterfaz();
        conectarEventos();
    }

    // Se llama cada vez que se abre esta pantalla desde el menú
    void refrescar() {
        QVector<QStringList> filas = consultar(
            "SELECT id, usuario, COALESCE(email,''), rol, IF(activo=1,'Sí','No') "
            "FROM usuarios ORDER BY usuario");
        llenarTabla(tabla, filas);
    }

private:
    void crearInterfaz() {
        QVBoxLayout &raiz = crearLayoutVertical(*this);
        configurarTitulo(titulo, "Usuarios del sistema");
        raiz.addWidget(&titulo);

        configurarTabla(tabla, QStringList() << "ID" << "Usuario" << "Email" << "Rol" << "Activo");
        raiz.addWidget(&tabla, 1);

        // --- Formulario ---
        formulario.setTitle("Datos del usuario");
        QFormLayout &form = crearLayoutFormulario(formulario);

        entPassword.setEchoMode(QLineEdit::Password);
        entPassword.setPlaceholderText("Mínimo 6 caracteres (al modificar, vacío = no cambiar)");
        cmbRol.addItem("vendedor");
        cmbRol.addItem("administrador");
        chkActivo.setText("Usuario activo");
        chkActivo.setChecked(true);

        form.addRow("Usuario:", &entUsuario);
        form.addRow("Email:", &entEmail);
        form.addRow("Contraseña:", &entPassword);
        form.addRow("Rol:", &cmbRol);
        form.addRow("", &chkActivo);

        configurarBoton(btnCrear, "Crear usuario", "exito");
        configurarBoton(btnMod, "Modificar", "aviso");
        configurarBoton(btnDel, "Eliminar", "peligro");
        configurarBoton(btnLimpiar, "Limpiar");
        QHBoxLayout &botones = agregarFilaAlFormulario(form);
        botones.addWidget(&btnCrear);
        botones.addWidget(&btnMod);
        botones.addWidget(&btnDel);
        botones.addWidget(&btnLimpiar);

        raiz.addWidget(&formulario);
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&tabla, &QTableWidget::itemSelectionChanged, this, &PaginaUsuarios::seleccionarFila);

        connect(&btnCrear, &QPushButton::clicked, this, &PaginaUsuarios::crear);
        connect(&btnMod, &QPushButton::clicked, this, &PaginaUsuarios::modificar);
        connect(&btnDel, &QPushButton::clicked, this, &PaginaUsuarios::eliminar);
        connect(&btnLimpiar, &QPushButton::clicked, this, &PaginaUsuarios::limpiar);
    }

    // Al elegir una fila, se copian sus datos al formulario
    // Se llama desde: un cambio de selección en la tabla
    void seleccionarFila() {
        int fila = filaSeleccionada(tabla);
        if (fila < 0)
            return;

        idSel = textoCelda(tabla, fila, 0).toInt();
        entUsuario.setText(textoCelda(tabla, fila, 1));
        entEmail.setText(textoCelda(tabla, fila, 2));
        entPassword.clear();
        cmbRol.setCurrentText(textoCelda(tabla, fila, 3));
        chkActivo.setChecked(textoCelda(tabla, fila, 4) == "Sí");
    }

    // Se llama desde: el botón "Limpiar" y después de guardar o eliminar
    void limpiar() {
        idSel = 0;
        entUsuario.clear();
        entEmail.clear();
        entPassword.clear();
        cmbRol.setCurrentIndex(0);
        chkActivo.setChecked(true);
        tabla.clearSelection();
    }

    // Se llama desde: el botón "Crear usuario"
    void crear() {
        QString usuario = entUsuario.text().trimmed();
        QString password = entPassword.text();

        if (usuario.isEmpty()) {
            aviso(this, "El nombre de usuario es obligatorio.");
            return;
        }
        if (password.size() < 6) {
            aviso(this, "La contraseña debe tener al menos 6 caracteres.");
            return;
        }

        // Se guarda el hash de la contraseña, nunca la contraseña
        QString salt = generarSalt();
        QString activo = "0";
        if (chkActivo.isChecked())
            activo = "1";

        QString sql = "INSERT INTO usuarios (usuario, password, salt, email, rol, activo) VALUES (" +
                      sqlTxt(usuario) + ", '" + hashPassword(salt, password) + "', '" + salt + "', " +
                      sqlTxt(entEmail.text()) + ", '" + cmbRol.currentText() + "', " + activo + ")";

        if (ejecutar(sql)) {
            info(this, "Usuario '" + usuario + "' creado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo crear el usuario (¿ya existe ese nombre?):\n" + errorBD());
        }
    }

    // Se llama desde: el botón "Modificar"
    void modificar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un usuario de la tabla.");
            return;
        }
        QString usuario = entUsuario.text().trimmed();
        if (usuario.isEmpty()) {
            aviso(this, "El nombre de usuario es obligatorio.");
            return;
        }
        // Para no quedarse sin acceso, no puede quitarse su propio permiso
        if (idSel == sesionId && (!chkActivo.isChecked() || cmbRol.currentText() != "administrador")) {
            aviso(this, "No puedes desactivarte ni quitarte el rol de administrador a ti mismo.");
            return;
        }
        QString password = entPassword.text();
        if (!password.isEmpty() && password.size() < 6) {
            aviso(this, "La contraseña debe tener al menos 6 caracteres.");
            return;
        }

        // Solo se cambia la contraseña si escribieron una nueva
        QString cambioPassword = "";
        if (!password.isEmpty()) {
            QString salt = generarSalt();
            cambioPassword = ", password='" + hashPassword(salt, password) + "', salt='" + salt + "'";
        }

        QString activo = "0";
        if (chkActivo.isChecked())
            activo = "1";

        QString sql = "UPDATE usuarios SET usuario=" + sqlTxt(usuario) +
                      ", email=" + sqlTxt(entEmail.text()) +
                      ", rol='" + cmbRol.currentText() + "'" +
                      ", activo=" + activo + cambioPassword +
                      " WHERE id=" + QString::number(idSel);

        if (ejecutar(sql)) {
            info(this, "Usuario actualizado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo actualizar: " + errorBD());
        }
    }

    // Se llama desde: el botón "Eliminar"
    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un usuario de la tabla.");
            return;
        }
        if (idSel == sesionId) {
            aviso(this, "No puedes eliminar tu propio usuario.");
            return;
        }
        if (!confirmar(this, "¿Eliminar al usuario '" + entUsuario.text() + "'?"))
            return;

        if (ejecutar("DELETE FROM usuarios WHERE id = " + QString::number(idSel))) {
            info(this, "Usuario eliminado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo eliminar. Si el usuario ya registró ventas, "
                        "desactívalo en lugar de eliminarlo.");
        }
    }
};

// =============================================================================
// 11. PÁGINA DE REPORTES (se pueden exportar a .txt)
// El vendedor solo ve sus propias ventas; el administrador ve todo.
// =============================================================================
class PaginaReportes : public QWidget {
    QLabel titulo;
    QComboBox cmbTipo;
    QDateEdit dDesde;
    QDateEdit dHasta;
    QPushButton btnGenerar;
    QPushButton btnExportar;
    QTextEdit vista;
    QString nombreArchivo = "reporte";

public:
    PaginaReportes() {
        crearInterfaz();
        conectarEventos();
    }

    // Se llama cada vez que se abre esta pantalla desde el menú
    void refrescar() {
        // Los reportes se generan solo al pulsar "Generar"
    }

private:
    void crearInterfaz() {
        QVBoxLayout &raiz = crearLayoutVertical(*this);
        configurarTitulo(titulo, "Reportes");
        raiz.addWidget(&titulo);

        // Tipos de reporte (el segundo dato es una clave interna)
        cmbTipo.addItem("Ventas del periodo", "ventas");
        cmbTipo.addItem("Productos vendidos", "vendidos");
        cmbTipo.addItem("Corte de caja (por método de pago)", "corte");
        if (esAdmin())
            cmbTipo.addItem("Ventas por vendedor", "vendedores");
        cmbTipo.addItem("Inventario actual", "inventario");
        cmbTipo.addItem("Productos con existencia baja", "stock");
        cmbTipo.addItem("Clientes y total comprado", "clientes");

        // Fechas (por defecto: del día 1 del mes a hoy)
        QDate hoy = QDate::currentDate();
        dDesde.setDate(QDate(hoy.year(), hoy.month(), 1));
        dHasta.setDate(hoy);
        dDesde.setCalendarPopup(true);
        dHasta.setCalendarPopup(true);
        dDesde.setDisplayFormat("yyyy-MM-dd");
        dHasta.setDisplayFormat("yyyy-MM-dd");

        QHBoxLayout &fila = agregarFila(raiz);
        agregarTexto(fila, "Reporte:");
        fila.addWidget(&cmbTipo, 1);
        agregarTexto(fila, "Desde:");
        fila.addWidget(&dDesde);
        agregarTexto(fila, "Hasta:");
        fila.addWidget(&dHasta);

        // Botones
        configurarBoton(btnGenerar, "Generar");
        configurarBoton(btnExportar, "Exportar a .txt");
        QHBoxLayout &filaBotones = agregarFila(raiz);
        filaBotones.addWidget(&btnGenerar);
        filaBotones.addWidget(&btnExportar);
        filaBotones.addStretch();

        // Vista previa del reporte
        vista.setReadOnly(true);
        vista.setLineWrapMode(QTextEdit::NoWrap);
        vista.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        raiz.addWidget(&vista, 1);
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&btnGenerar, &QPushButton::clicked, this, &PaginaReportes::generar);
        connect(&btnExportar, &QPushButton::clicked, this, &PaginaReportes::exportar);
        connect(&cmbTipo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaginaReportes::activarFechas);
    }

    // Algunos reportes no usan fechas
    // Se llama desde: el cambio de tipo de reporte
    void activarFechas() {
        QString clave = cmbTipo.currentData().toString();
        bool usaFechas = true;
        if (clave == "inventario" || clave == "stock" || clave == "clientes")
            usaFechas = false;
        dDesde.setEnabled(usaFechas);
        dHasta.setEnabled(usaFechas);
    }

    // Se llama desde: el botón "Exportar a .txt"
    void exportar() {
        if (vista.toPlainText().isEmpty()) {
            aviso(this, "Primero genera un reporte.");
            return;
        }
        exportarTxt(this, nombreArchivo, vista.toPlainText());
    }

    // Arma el reporte elegido y lo muestra en pantalla
    // Se llama desde: el botón "Generar"
    void generar() {
        QString clave = cmbTipo.currentData().toString();
        bool usaFechas = dDesde.isEnabled();

        if (usaFechas && dDesde.date() > dHasta.date()) {
            aviso(this, "La fecha inicial no puede ser posterior a la final.");
            return;
        }

        QString desde = dDesde.date().toString("yyyy-MM-dd");
        QString hasta = dHasta.date().toString("yyyy-MM-dd");
        QString rango = "DATE(v.fecha) BETWEEN '" + desde + "' AND '" + hasta + "'";

        // El vendedor solo ve sus ventas
        QString soloMias = "";
        if (!esAdmin())
            soloMias = " AND v.usuario_id = " + QString::number(sesionId);

        nombreArchivo = "reporte_" + clave;

        // Elegir qué reporte armar
        QString cuerpo;
        if (clave == "ventas")
            cuerpo = reporteVentas(rango, soloMias);
        else if (clave == "vendidos")
            cuerpo = reporteVendidos(rango, soloMias);
        else if (clave == "corte")
            cuerpo = reporteCorte(rango, soloMias);
        else if (clave == "vendedores")
            cuerpo = reporteVendedores(rango);
        else if (clave == "inventario")
            cuerpo = reporteInventario();
        else if (clave == "stock")
            cuerpo = reporteStockBajo();
        else if (clave == "clientes")
            cuerpo = reporteClientes();

        // Encabezado del reporte
        QString linea = QString("=").repeated(60);
        QString txt = linea + "\n";
        txt += NOMBRE_NEGOCIO + " — " + cmbTipo.currentText().toUpper() + "\n";
        txt += "Generado: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") +
               "   Por: " + sesionUsuario + " (" + sesionRol + ")\n";
        if (usaFechas)
            txt += "Periodo:  " + desde + " al " + hasta + "\n";
        if (usaFechas && !esAdmin())
            txt += "Solo incluye tus propias ventas.\n";
        txt += linea + "\n\n";
        txt += cuerpo;

        vista.setPlainText(txt);
    }

    // ---------- Un método por cada tipo de reporte ----------
    // Todos funcionan igual: 1) consultan, 2) escriben los encabezados,
    // 3) recorren las filas con un ciclo for, 4) agregan el total al final.
    // Se llaman desde: generar()

    QString reporteVentas(const QString &rango, const QString &soloMias) {
        QVector<QStringList> filas = consultar(
            "SELECT v.id, v.fecha, COALESCE(c.nombre,'Público general'), u.usuario, v.metodo_pago, v.total "
            "FROM ventas v JOIN usuarios u ON u.id = v.usuario_id "
            "LEFT JOIN clientes c ON c.id = v.cliente_id "
            "WHERE " + rango + soloMias + " ORDER BY v.fecha");

        QString texto;
        texto += textoColumna("Folio", 6) + textoColumna("Fecha", 19) + textoColumna("Cliente", 22) +
                 textoColumna("Vendedor", 12) + textoColumna("Pago", 13) + textoColumna("Total", 12, true) + "\n";
        texto += QString("-").repeated(94) + "\n";

        double suma = 0;
        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            suma += fila[5].toDouble();
            texto += textoColumna(fila[0], 6) + textoColumna(fila[1], 19) + textoColumna(fila[2], 22) +
                     textoColumna(fila[3], 12) + textoColumna(fila[4], 13) +
                     textoColumna(dinero(fila[5].toDouble()), 12, true) + "\n";
        }

        texto += "\nVentas: " + QString::number(filas.size()) + "     Total vendido: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteVendidos(const QString &rango, const QString &soloMias) {
        QVector<QStringList> filas = consultar(
            "SELECT d.nombre_articulo, SUM(d.cantidad), SUM(d.subtotal) FROM detalle_venta d "
            "JOIN ventas v ON v.id = d.venta_id WHERE " + rango + soloMias +
            " GROUP BY d.nombre_articulo ORDER BY SUM(d.cantidad) DESC");

        QString texto;
        texto += textoColumna("Producto", 34) + textoColumna("Unidades", 10, true) +
                 textoColumna("Importe", 14, true) + "\n";
        texto += QString("-").repeated(62) + "\n";

        double suma = 0;
        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            suma += fila[2].toDouble();
            texto += textoColumna(fila[0], 34) + textoColumna(fila[1], 10, true) +
                     textoColumna(dinero(fila[2].toDouble()), 14, true) + "\n";
        }

        texto += "\nImporte total: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteCorte(const QString &rango, const QString &soloMias) {
        QVector<QStringList> filas = consultar(
            "SELECT v.metodo_pago, COUNT(*), SUM(v.total) FROM ventas v WHERE " + rango + soloMias +
            " GROUP BY v.metodo_pago ORDER BY v.metodo_pago");

        QString texto;
        texto += textoColumna("Método de pago", 20) + textoColumna("Ventas", 8, true) +
                 textoColumna("Total", 14, true) + "\n";
        texto += QString("-").repeated(46) + "\n";

        double suma = 0;
        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            suma += fila[2].toDouble();
            texto += textoColumna(fila[0], 20) + textoColumna(fila[1], 8, true) +
                     textoColumna(dinero(fila[2].toDouble()), 14, true) + "\n";
        }

        texto += "\nTotal en caja: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteVendedores(const QString &rango) {
        QVector<QStringList> filas = consultar(
            "SELECT u.usuario, COUNT(*), SUM(v.total) FROM ventas v "
            "JOIN usuarios u ON u.id = v.usuario_id WHERE " + rango +
            " GROUP BY u.id ORDER BY SUM(v.total) DESC");

        QString texto;
        texto += textoColumna("Vendedor", 24) + textoColumna("Ventas", 8, true) +
                 textoColumna("Total", 14, true) + "\n";
        texto += QString("-").repeated(50) + "\n";

        double suma = 0;
        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            suma += fila[2].toDouble();
            texto += textoColumna(fila[0], 24) + textoColumna(fila[1], 8, true) +
                     textoColumna(dinero(fila[2].toDouble()), 14, true) + "\n";
        }

        texto += "\nTotal general: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteInventario() {
        QVector<QStringList> filas = consultar(
            "SELECT id, nombre_articulo, COALESCE(categoria,''), existencia, precio, existencia*precio "
            "FROM productos ORDER BY categoria, nombre_articulo");

        QString texto;
        texto += textoColumna("ID", 4, true) + textoColumna("Artículo", 26) + textoColumna("Categoría", 16) +
                 textoColumna("Exist.", 7, true) + textoColumna("Precio", 10, true) +
                 textoColumna("Valor", 12, true) + "\n";
        texto += QString("-").repeated(85) + "\n";

        double valor = 0;
        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            valor += fila[5].toDouble();
            texto += textoColumna(fila[0], 4, true) + textoColumna(fila[1], 26) + textoColumna(fila[2], 16) +
                     textoColumna(fila[3], 7, true) + textoColumna(dinero(fila[4].toDouble()), 10, true) +
                     textoColumna(dinero(fila[5].toDouble()), 12, true) + "\n";
        }

        texto += "\nProductos: " + QString::number(filas.size()) + "     Valor del inventario: " + dinero(valor) + "\n";
        return texto;
    }

    QString reporteStockBajo() {
        QVector<QStringList> filas = consultar(
            "SELECT p.id, p.nombre_articulo, COALESCE(p.categoria,''), p.existencia, "
            "COALESCE(pr.nombre_empresa,'—') FROM productos p "
            "LEFT JOIN proveedores pr ON pr.id = p.proveedor_id "
            "WHERE p.existencia <= " + QString::number(STOCK_BAJO) +
            " ORDER BY p.existencia, p.nombre_articulo");

        QString texto = "Productos con existencia menor o igual a " + QString::number(STOCK_BAJO) + "\n\n";
        texto += textoColumna("ID", 4, true) + textoColumna("Artículo", 26) + textoColumna("Categoría", 16) +
                 textoColumna("Exist.", 7, true) + textoColumna("Proveedor", 24) + "\n";
        texto += QString("-").repeated(85) + "\n";

        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            texto += textoColumna(fila[0], 4, true) + textoColumna(fila[1], 26) + textoColumna(fila[2], 16) +
                     textoColumna(fila[3], 7, true) + textoColumna(fila[4], 24) + "\n";
        }

        texto += "\nProductos por reabastecer: " + QString::number(filas.size()) + "\n";
        return texto;
    }

    QString reporteClientes() {
        QVector<QStringList> filas = consultar(
            "SELECT c.id, c.nombre, COALESCE(c.telefono,''), COALESCE(c.email,''), COUNT(v.id), "
            "COALESCE(SUM(v.total),0) FROM clientes c LEFT JOIN ventas v ON v.cliente_id = c.id "
            "GROUP BY c.id ORDER BY SUM(v.total) DESC, c.nombre");

        QString texto;
        texto += textoColumna("ID", 4, true) + textoColumna("Nombre", 24) + textoColumna("Teléfono", 12) +
                 textoColumna("Email", 26) + textoColumna("Compras", 8, true) +
                 textoColumna("Total", 12, true) + "\n";
        texto += QString("-").repeated(96) + "\n";

        for (int i = 0; i < filas.size(); i++) {
            QStringList fila = filas[i];
            texto += textoColumna(fila[0], 4, true) + textoColumna(fila[1], 24) + textoColumna(fila[2], 12) +
                     textoColumna(fila[3], 26) + textoColumna(fila[4], 8, true) +
                     textoColumna(dinero(fila[5].toDouble()), 12, true) + "\n";
        }
        return texto;
    }
};

// =============================================================================
// 12. VENTANA PRINCIPAL (menú lateral + páginas según el rol)
// =============================================================================
class VentanaPrincipal : public QWidget {
    // El panel es un contenedor: se declara ANTES de lo que contiene
    QWidget panel;
    QLabel lblNegocio;
    QListWidget menu;
    QLabel lblSesion;
    QPushButton btnSalir;

    // El StackedWidget también es un contenedor: se declara ANTES de las páginas
    QStackedWidget paginas;
    PaginaVentas paginaVentas;
    PaginaInventario paginaInventario;
    PaginaClientes paginaClientes;
    PaginaUsuarios paginaUsuarios;      // solo se muestra si el usuario es administrador
    PaginaReportes paginaReportes;

    QStringList nombresPaginas;         // nombre de cada opción del menú, en orden
    QWidget &ventanaLogin;              // para volver al login al cerrar sesión

public:
    VentanaPrincipal(QWidget &login) : ventanaLogin(login) {
        setWindowTitle(NOMBRE_NEGOCIO + " - Punto de Venta");
        resize(1150, 720);

        crearInterfaz();
        conectarEventos();

        menu.setCurrentRow(0);          // abre la primera pantalla (Ventas)
    }

private:
    void crearInterfaz() {
        QHBoxLayout &raiz = crearLayoutHorizontal(*this);
        raiz.setContentsMargins(14, 14, 14, 14);
        raiz.setSpacing(14);

        // --- Panel lateral ---
        panel.setObjectName("sidebar");
        panel.setFixedWidth(220);
        QVBoxLayout &layoutPanel = crearLayoutVertical(panel);
        layoutPanel.setContentsMargins(12, 14, 12, 14);

        lblNegocio.setText(NOMBRE_NEGOCIO);
        lblNegocio.setObjectName("negocio");
        lblNegocio.setWordWrap(true);
        layoutPanel.addWidget(&lblNegocio);

        menu.setObjectName("menu");
        layoutPanel.addWidget(&menu, 1);

        lblSesion.setText("Usuario: " + sesionUsuario + "\nRol: " + sesionRol);
        lblSesion.setObjectName("sesion");
        layoutPanel.addWidget(&lblSesion);

        configurarBoton(btnSalir, "Cerrar sesión");
        layoutPanel.addWidget(&btnSalir);

        raiz.addWidget(&panel);

        // --- Zona de páginas ---
        raiz.addWidget(&paginas, 1);

        // El orden aquí es el orden del menú. "Usuarios" solo lo ve el administrador.
        agregarPagina("Ventas", paginaVentas);
        agregarPagina("Inventario", paginaInventario);
        agregarPagina("Clientes", paginaClientes);
        if (esAdmin())
            agregarPagina("Usuarios", paginaUsuarios);
        agregarPagina("Reportes", paginaReportes);
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&menu, &QListWidget::currentRowChanged, this, &VentanaPrincipal::cambiarPagina);
        connect(&btnSalir, &QPushButton::clicked, this, &VentanaPrincipal::cerrarSesion);
    }

    // Agrega una opción al menú y su pantalla a la zona de páginas
    void agregarPagina(const QString &nombre, QWidget &pagina) {
        menu.addItem(nombre);
        paginas.addWidget(&pagina);
        nombresPaginas << nombre;
    }

    // Al cambiar de opción en el menú se muestra la página y se actualizan sus datos
    // Se llama desde: un clic en el menú lateral
    void cambiarPagina(int fila) {
        if (fila < 0)
            return;

        QString nombre = nombresPaginas[fila];
        if (nombre == "Ventas") {
            paginas.setCurrentWidget(&paginaVentas);
            paginaVentas.refrescar();
        } else if (nombre == "Inventario") {
            paginas.setCurrentWidget(&paginaInventario);
            paginaInventario.refrescar();
        } else if (nombre == "Clientes") {
            paginas.setCurrentWidget(&paginaClientes);
            paginaClientes.refrescar();
        } else if (nombre == "Usuarios") {
            paginas.setCurrentWidget(&paginaUsuarios);
            paginaUsuarios.refrescar();
        } else if (nombre == "Reportes") {
            paginas.setCurrentWidget(&paginaReportes);
            paginaReportes.refrescar();
        }
    }

    // Se llama desde: el botón "Cerrar sesión"
    void cerrarSesion() {
        if (!confirmar(this, "¿Cerrar la sesión?"))
            return;
        sesionId = 0;
        sesionUsuario = "";
        sesionRol = "";
        ventanaLogin.show();
        close();
    }
};

// Crea la ventana principal y la muestra maximizada.
// Se hace con "new" porque la ventana se borra sola al cerrarse (WA_DeleteOnClose).
// Se llama desde: VentanaLogin::intentarLogin()
void abrirVentanaPrincipal(QWidget &login) {
    VentanaPrincipal *principal = new VentanaPrincipal(login);
    principal->setAttribute(Qt::WA_DeleteOnClose);
    principal->showMaximized();
}

// =============================================================================
// 13. VENTANA DE LOGIN
// =============================================================================
class VentanaLogin : public QWidget {
    // La tarjeta es un contenedor: se declara ANTES de lo que contiene
    QFrame tarjeta;
    QLabel titulo;
    QLabel subtitulo;
    QLineEdit entUsuario;
    QLineEdit entPassword;
    QPushButton btnIngresar;

public:
    VentanaLogin() {
        setWindowTitle("Acceso - " + NOMBRE_NEGOCIO);
        setObjectName("login");
        resize(400, 400);

        crearInterfaz();
        conectarEventos();
    }

private:
    void crearInterfaz() {
        QVBoxLayout &externo = crearLayoutVertical(*this);
        externo.setContentsMargins(30, 30, 30, 30);
        tarjeta.setObjectName("tarjeta");
        externo.addWidget(&tarjeta);

        QVBoxLayout &layout = crearLayoutVertical(tarjeta);
        layout.setContentsMargins(28, 28, 28, 28);
        layout.setSpacing(12);

        titulo.setText("Iniciar Sesión");
        titulo.setObjectName("tituloLogin");
        layout.addWidget(&titulo, 0, Qt::AlignCenter);

        subtitulo.setText(NOMBRE_NEGOCIO);
        subtitulo.setObjectName("subtituloLogin");
        layout.addWidget(&subtitulo, 0, Qt::AlignCenter);
        layout.addStretch();

        entUsuario.setPlaceholderText("Usuario");
        layout.addWidget(&entUsuario);

        entPassword.setPlaceholderText("Contraseña");
        entPassword.setEchoMode(QLineEdit::Password);
        layout.addWidget(&entPassword);

        configurarBoton(btnIngresar, "Ingresar", "primario");
        layout.addWidget(&btnIngresar);
        layout.addStretch();
    }

    // ---------- Conexiones: aquí se ve qué función responde a cada evento ----------
    void conectarEventos() {
        connect(&btnIngresar, &QPushButton::clicked, this, &VentanaLogin::intentarLogin);
        connect(&entPassword, &QLineEdit::returnPressed, this, &VentanaLogin::intentarLogin);
        connect(&entUsuario, &QLineEdit::returnPressed, this, &VentanaLogin::pasarAPassword);
    }

    // Al pulsar Enter en el campo de usuario, el cursor pasa a la contraseña
    // Se llama desde: la tecla Enter en el campo "Usuario"
    void pasarAPassword() {
        entPassword.setFocus();
    }

    // Se llama desde: el botón "Ingresar" y la tecla Enter en el campo "Contraseña"
    void intentarLogin() {
        QString usuario = entUsuario.text().trimmed();
        QString password = entPassword.text();

        if (usuario.isEmpty() || password.isEmpty()) {
            aviso(this, "Por favor llena todos los campos");
            return;
        }

        // Buscar el usuario en la base de datos
        QVector<QStringList> filas = consultar(
            "SELECT id, usuario, password, salt, rol, activo FROM usuarios "
            "WHERE usuario = " + sqlTxt(usuario));

        // Columnas: 0=id 1=usuario 2=password 3=salt 4=rol 5=activo
        if (filas.isEmpty() || hashPassword(filas[0][3], password) != filas[0][2]) {
            aviso(this, "Usuario o contraseña incorrectos.");
            return;
        }
        if (filas[0][5] != "1") {
            aviso(this, "Este usuario está desactivado. Contacta al administrador.");
            return;
        }

        // Guardar los datos de la sesión
        sesionId = filas[0][0].toInt();
        sesionUsuario = filas[0][1];
        sesionRol = filas[0][4];

        // Ocultar el login y abrir la ventana principal
        entPassword.clear();
        entUsuario.setFocus();
        hide();
        abrirVentanaPrincipal(*this);
    }
};

// =============================================================================
// 14. MAIN
// =============================================================================
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AbarroteraDonJose");
    QCoreApplication::setApplicationName("PuntoDeVenta");
    app.setStyle("Fusion");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);   // siempre tema claro
#endif
    cargarEstilo(app);

    // Datos de conexión (config.ini > valores por defecto). La contraseña NO va en el código.
    QString host = leerConfig("host", "DB_HOST", "bulrush.cloud.layerbase.dev");
    int puerto = leerConfig("puerto", "DB_PORT", "10086").toInt();
    QString usuario = leerConfig("usuario", "DB_USER", "root");
    QString password = leerConfig("password", "DB_PASS", "");
    QString base = leerConfig("nombre", "DB_NAME", "abarrotesdb");

    if (password.isEmpty()) {
        QMessageBox::critical(nullptr, "Falta la contraseña",
                              "No hay contraseña de la base de datos.\n\n"
                              "Escríbela en el archivo config.ini (junto al programa), en la línea "
                              "password=");
        return 1;
    }

    // Aviso mientras se conecta (puede tardar si la base estaba dormida)
    QLabel espera("Conectando con el servidor...\n"
                  "Si la base de datos estaba en reposo puede tardar hasta 30 segundos.");
    espera.setWindowTitle("Conectando");
    espera.setAlignment(Qt::AlignCenter);
    espera.resize(420, 120);
    espera.show();
    app.processEvents();

    QString detalle;
    bool conectado = conectarBaseDatos(host, puerto, usuario, password, base, detalle);
    espera.close();

    if (!conectado) {
        QMessageBox::critical(nullptr, "Error de Conexión",
                              "No se pudo conectar a la base de datos.\n\n"
                              "Servidor: " + host + ":" + QString::number(puerto) + "\n"
                                                                           "Detalle: " + detalle + "\n\n"
                                              "Revisa tu conexión a internet y los datos de config.ini.");
        return 1;
    }

    migrarPasswords();

    VentanaLogin login;
    login.show();

    int resultado = app.exec();
    cerrarConexion();
    return resultado;
}
}
