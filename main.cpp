// =============================================================================
// PUNTO DE VENTA - Abarrotera Don José (versión sencilla)
// Qt + MariaDB Connector/C
//
// Pantallas: Login, Ventas, Inventario, Clientes, Usuarios (admin), Reportes
// Roles:     administrador (todo)  |  vendedor (ventas, consulta, clientes, sus reportes)
//
// Requiere haber ejecutado migracion.sql sobre la base de datos.
// La conexión al servidor se configura en config.ini (junto al .exe).
// =============================================================================
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
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
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QSettings>
#include <mysql.h>

// =============================================================================
// 1. VARIABLES GLOBALES
// =============================================================================
const int STOCK_BAJO = 10;   // existencia menor o igual a esto = "bajo"
const int TAM_ICONO = 104;   // tamaño de las imágenes del catálogo
const QString NOMBRE_NEGOCIO = "Abarrotera Don José";

MYSQL *conexion = nullptr;

// Datos del usuario que inició sesión
int sesionId = 0;
QString sesionUsuario;
QString sesionRol;

bool esAdmin() {
    return sesionRol == "administrador";
}

// =============================================================================
// 2. BASE DE DATOS
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
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(resultado))) {
        QStringList fila;
        for (unsigned int i = 0; i < columnas; i++) {
            if (row[i])
                fila << QString::fromUtf8(row[i]);
            else
                fila << QString();
        }
        filas.push_back(fila);
    }
    mysql_free_result(resultado);
    return filas;
}

// =============================================================================
// 3. CONTRASEÑAS (SHA-256 con sal)
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
    auto filas = consultar("SELECT id, password FROM usuarios WHERE salt IS NULL OR salt = ''");
    for (const auto &fila : filas) {
        QString salt = generarSalt();
        QString sql = "UPDATE usuarios SET salt='" + salt + "', password='" +
                      hashPassword(salt, fila[1]) + "' WHERE id=" + fila[0];
        ejecutar(sql);
    }
}

// =============================================================================
// 4. FUNCIONES DE AYUDA PARA LA INTERFAZ
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

QLabel *crearTitulo(const QString &texto) {
    QLabel *titulo = new QLabel(texto);
    titulo->setObjectName("titulo");
    return titulo;
}

// Carga el archivo de estilos estilo_claro.qss
void cargarEstilo() {
    QStringList carpetas = {QCoreApplication::applicationDirPath(), QDir::currentPath(), ":"};
    for (const QString &carpeta : carpetas) {
        QFile archivo(carpeta + "/estilo_claro.qss");
        if (archivo.open(QIODevice::ReadOnly)) {
            qApp->setStyleSheet(QString::fromUtf8(archivo.readAll()));
            return;
        }
    }
    qWarning() << "No se encontró estilo_claro.qss, se usa el estilo por defecto.";
}

// Crea una tabla con encabezados ya configurada
QTableWidget *crearTabla(const QStringList &encabezados) {
    QTableWidget *tabla = new QTableWidget(0, encabezados.size());
    tabla->setHorizontalHeaderLabels(encabezados);
    tabla->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabla->setSelectionMode(QAbstractItemView::SingleSelection);
    tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabla->horizontalHeader()->setStretchLastSection(true);
    tabla->verticalHeader()->setVisible(false);
    tabla->setAlternatingRowColors(true);
    return tabla;
}

void llenarTabla(QTableWidget *tabla, const QVector<QStringList> &filas) {
    tabla->clearSelection();
    tabla->setRowCount(filas.size());
    for (int r = 0; r < filas.size(); r++) {
        for (int c = 0; c < filas[r].size(); c++) {
            tabla->setItem(r, c, new QTableWidgetItem(filas[r][c]));
        }
    }
    tabla->resizeColumnsToContents();
}

// Crea un botón con un estilo ("exito", "aviso", "peligro"...)
QPushButton *crearBoton(const QString &texto, const QString &estilo = "") {
    QPushButton *boton = new QPushButton(texto);
    if (!estilo.isEmpty())
        boton->setProperty("estilo", estilo);
    return boton;
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
        QPixmap escalada = imagen.scaled(tam, tam, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pintor.drawPixmap((tam - escalada.width()) / 2, (tam - escalada.height()) / 2, escalada);
    } else {
        // Un color distinto por categoría
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

// =============================================================================
// 6. TEXTO PARA REPORTES Y TICKETS
// =============================================================================

// Arma una tabla de texto con columnas de ancho fijo.
// alineacion: una letra por columna, 'L' = izquierda, 'R' = derecha
QString tablaTxt(const QStringList &encabezados, const QVector<QStringList> &filas,
                 const QVector<int> &anchos, const QString &alineacion) {
    QString salida;
    int anchoTotal = 0;

    // Encabezados
    QString linea;
    for (int i = 0; i < encabezados.size(); i++) {
        if (alineacion[i] == 'R')
            linea += encabezados[i].rightJustified(anchos[i], ' ', true);
        else
            linea += encabezados[i].leftJustified(anchos[i], ' ', true);
        linea += "  ";
        anchoTotal += anchos[i] + 2;
    }
    salida += linea.trimmed() + "\n";
    salida += QString("-").repeated(anchoTotal - 2) + "\n";

    // Filas
    for (const auto &fila : filas) {
        linea = "";
        for (int i = 0; i < encabezados.size(); i++) {
            if (alineacion[i] == 'R')
                linea += fila[i].rightJustified(anchos[i], ' ', true);
            else
                linea += fila[i].leftJustified(anchos[i], ' ', true);
            linea += "  ";
        }
        salida += linea.trimmed() + "\n";
    }
    return salida;
}

// Da formato de dinero a una columna y regresa la suma de esa columna
double formatearColumnaDinero(QVector<QStringList> &filas, int columna) {
    double suma = 0;
    for (auto &fila : filas) {
        double valor = fila[columna].toDouble();
        suma += valor;
        fila[columna] = dinero(valor);
    }
    return suma;
}

// Guarda un texto en un archivo .txt elegido por el usuario
void exportarTxt(QWidget *padre, const QString &nombreBase, const QString &contenido) {
    QString carpeta = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString sugerido = carpeta + "/" + nombreBase + "_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".txt";
    QString ruta = QFileDialog::getSaveFileName(padre, "Guardar como", sugerido, "Archivos de texto (*.txt)");
    if (ruta.isEmpty())
        return;

    QFile archivo(ruta);
    if (!archivo.open(QIODevice::WriteOnly | QIODevice::Text)) {
        falla(padre, "No se pudo escribir el archivo:\n" + ruta);
        return;
    }
    archivo.write(contenido.toUtf8());
    archivo.close();
    info(padre, "Archivo guardado en:\n" + ruta);
}

// Muestra el ticket en una ventana con opción de guardarlo
void mostrarTicket(QWidget *padre, const QString &texto, const QString &nombreBase) {
    QDialog ventana(padre);
    ventana.setWindowTitle("Ticket de venta");
    ventana.resize(520, 560);

    QVBoxLayout *layout = new QVBoxLayout(&ventana);
    QTextEdit *visor = new QTextEdit;
    visor->setReadOnly(true);
    visor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    visor->setPlainText(texto);
    layout->addWidget(visor);

    QPushButton *btnGuardar = new QPushButton("Guardar como .txt");
    QPushButton *btnCerrar = new QPushButton("Cerrar");
    QHBoxLayout *botones = new QHBoxLayout;
    botones->addWidget(btnGuardar);
    botones->addWidget(btnCerrar);
    layout->addLayout(botones);

    QObject::connect(btnGuardar, &QPushButton::clicked, &ventana, [&]() {
        exportarTxt(&ventana, nombreBase, texto);
    });
    QObject::connect(btnCerrar, &QPushButton::clicked, &ventana, &QDialog::accept);
    ventana.exec();
}

// =============================================================================
// 7. CLASE BASE DE LAS PÁGINAS
// Cada pantalla del menú hereda de aquí y define cómo actualizar sus datos
// =============================================================================
class Pagina : public QWidget {
public:
    using QWidget::QWidget;
    virtual void refrescar() = 0;
};

// =============================================================================
// 8. PÁGINA DE VENTAS
// =============================================================================
struct ItemCarrito {
    int id;
    QString nombre;
    double precio;
    int cantidad;
};

class PaginaVentas : public Pagina {
    QComboBox *cmbCliente;
    QComboBox *cmbPago;
    QComboBox *cmbCategoria;
    QLineEdit *buscar;
    QListWidget *listaProductos;
    QSpinBox *spCantidad;
    QDoubleSpinBox *spRecibido;
    QTableWidget *tabla;
    QLabel *lblTotal;
    QLabel *lblCambio;
    QVector<ItemCarrito> carrito;
    QHash<int, int> stockPorId;   // id del producto -> existencia

public:
    PaginaVentas(QWidget *padre = nullptr) : Pagina(padre) {
        crearInterfaz();
        conectarEventos();
    }

    void refrescar() override {
        cargarClientes();
        cargarProductos();
        filtrar();
        mostrarCarrito();
    }

private:
    // ---------- Interfaz ----------
    void crearInterfaz() {
        QVBoxLayout *raiz = new QVBoxLayout(this);
        raiz->addWidget(crearTitulo("Nueva venta"));

        QHBoxLayout *cuerpo = new QHBoxLayout;
        raiz->addLayout(cuerpo, 1);

        // --- Lado izquierdo: catálogo ---
        QVBoxLayout *izquierda = new QVBoxLayout;

        buscar = new QLineEdit;
        buscar->setPlaceholderText("Buscar producto...");
        buscar->setClearButtonEnabled(true);
        cmbCategoria = new QComboBox;
        cmbCategoria->setMinimumWidth(170);
        spCantidad = new QSpinBox;
        spCantidad->setRange(1, 100000);
        spCantidad->setPrefix("Cant. ");

        QHBoxLayout *filtros = new QHBoxLayout;
        filtros->addWidget(buscar, 1);
        filtros->addWidget(cmbCategoria);
        filtros->addWidget(spCantidad);
        izquierda->addLayout(filtros);

        listaProductos = new QListWidget;
        listaProductos->setObjectName("productos");
        listaProductos->setViewMode(QListView::IconMode);
        listaProductos->setResizeMode(QListView::Adjust);
        listaProductos->setMovement(QListView::Static);
        listaProductos->setIconSize(QSize(TAM_ICONO, TAM_ICONO));
        listaProductos->setGridSize(QSize(152, 184));
        listaProductos->setWordWrap(true);
        listaProductos->setSelectionMode(QAbstractItemView::NoSelection);
        listaProductos->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listaProductos->setCursor(Qt::PointingHandCursor);
        izquierda->addWidget(listaProductos, 1);
        cuerpo->addLayout(izquierda, 3);

        // --- Lado derecho: carrito y cobro ---
        QVBoxLayout *derecha = new QVBoxLayout;

        QHBoxLayout *filaCliente = new QHBoxLayout;
        filaCliente->addWidget(new QLabel("Cliente:"));
        cmbCliente = new QComboBox;
        filaCliente->addWidget(cmbCliente, 1);
        derecha->addLayout(filaCliente);

        tabla = crearTabla({"Producto", "Precio", "Cant.", "Subtotal"});
        tabla->setMinimumWidth(360);
        derecha->addWidget(tabla, 1);

        QPushButton *btnMenos = new QPushButton("−");
        QPushButton *btnMas = new QPushButton("+");
        QPushButton *btnQuitar = new QPushButton("Quitar");
        QPushButton *btnVaciar = new QPushButton("Vaciar");
        QHBoxLayout *filaCarrito = new QHBoxLayout;
        filaCarrito->addWidget(btnMenos);
        filaCarrito->addWidget(btnMas);
        filaCarrito->addWidget(btnQuitar);
        filaCarrito->addWidget(btnVaciar);
        derecha->addLayout(filaCarrito);

        cmbPago = new QComboBox;
        cmbPago->addItems({"Efectivo", "Tarjeta", "Transferencia"});
        QHBoxLayout *filaPago = new QHBoxLayout;
        filaPago->addWidget(new QLabel("Pago:"));
        filaPago->addWidget(cmbPago, 1);
        derecha->addLayout(filaPago);

        spRecibido = new QDoubleSpinBox;
        spRecibido->setRange(0, 10000000);
        spRecibido->setDecimals(2);
        spRecibido->setPrefix("$");
        lblCambio = new QLabel;
        QHBoxLayout *filaRecibido = new QHBoxLayout;
        filaRecibido->addWidget(new QLabel("Recibido:"));
        filaRecibido->addWidget(spRecibido, 1);
        filaRecibido->addWidget(lblCambio);
        derecha->addLayout(filaRecibido);

        lblTotal = new QLabel("Total: $0.00");
        lblTotal->setObjectName("total");
        lblTotal->setAlignment(Qt::AlignCenter);
        derecha->addWidget(lblTotal);

        QPushButton *btnCobrar = crearBoton("COBRAR", "exito");
        btnCobrar->setObjectName("cobrar");
        derecha->addWidget(btnCobrar);
        cuerpo->addLayout(derecha, 2);

        // Los botones del carrito se conectan aquí porque necesitan variables locales
        connect(btnMas, &QPushButton::clicked, this, [this]() { cambiarCantidad(+1); });
        connect(btnMenos, &QPushButton::clicked, this, [this]() { cambiarCantidad(-1); });
        connect(btnQuitar, &QPushButton::clicked, this, &PaginaVentas::quitarDelCarrito);
        connect(btnVaciar, &QPushButton::clicked, this, &PaginaVentas::vaciarCarrito);
        connect(btnCobrar, &QPushButton::clicked, this, &PaginaVentas::cobrar);
    }

    void conectarEventos() {
        connect(listaProductos, &QListWidget::itemClicked, this, &PaginaVentas::agregarAlCarrito);
        connect(buscar, &QLineEdit::textChanged, this, &PaginaVentas::filtrar);
        connect(cmbCategoria, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { filtrar(); });
        connect(cmbPago, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { actualizarCambio(); });
        connect(spRecibido, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double) { actualizarCambio(); });
    }

    // ---------- Cargar datos ----------
    void cargarClientes() {
        int clienteActual = cmbCliente->currentData().toInt();
        cmbCliente->clear();
        cmbCliente->addItem("Público general", 0);

        auto clientes = consultar("SELECT id, nombre FROM clientes ORDER BY nombre");
        for (const auto &c : clientes)
            cmbCliente->addItem(c[1], c[0].toInt());

        int pos = cmbCliente->findData(clienteActual);
        if (pos >= 0)
            cmbCliente->setCurrentIndex(pos);
        else
            cmbCliente->setCurrentIndex(0);
    }

    void cargarProductos() {
        QString categoriaActual = cmbCategoria->currentData().toString();

        auto productos = consultar(
            "SELECT id, nombre_articulo, existencia, precio, "
            "COALESCE(NULLIF(categoria,''),'Sin categoría'), COALESCE(imagen,'') "
            "FROM productos ORDER BY nombre_articulo");

        stockPorId.clear();
        listaProductos->clear();
        QStringList categorias;

        for (const auto &p : productos) {
            int id = p[0].toInt();
            QString nombre = p[1];
            int stock = p[2].toInt();
            double precio = p[3].toDouble();
            QString categoria = p[4];
            QString imagen = p[5];

            stockPorId[id] = stock;
            if (!categorias.contains(categoria))
                categorias << categoria;

            QString texto = nombre + "\n";
            if (stock > 0)
                texto += dinero(precio);
            else
                texto += "Agotado";

            QListWidgetItem *item = new QListWidgetItem(QIcon(imagenProducto(imagen, nombre, categoria)), texto);
            item->setSizeHint(QSize(142, 174));
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setToolTip(nombre + "\nExistencia: " + QString::number(stock));
            // Guardamos los datos del producto dentro del item
            item->setData(Qt::UserRole, id);
            item->setData(Qt::UserRole + 1, stock);
            item->setData(Qt::UserRole + 2, precio);
            item->setData(Qt::UserRole + 3, nombre);
            item->setData(Qt::UserRole + 4, categoria);
            if (stock <= 0)
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);   // no se puede elegir
            listaProductos->addItem(item);
        }

        // Llenar el filtro de categorías conservando la elección anterior
        categorias.sort(Qt::CaseInsensitive);
        cmbCategoria->blockSignals(true);
        cmbCategoria->clear();
        cmbCategoria->addItem("Todas las categorías", QString());
        for (const QString &c : categorias)
            cmbCategoria->addItem(c, c);
        int pos = cmbCategoria->findData(categoriaActual);
        if (pos >= 0)
            cmbCategoria->setCurrentIndex(pos);
        else
            cmbCategoria->setCurrentIndex(0);
        cmbCategoria->blockSignals(false);
    }

    // Oculta los productos que no coinciden con la búsqueda o la categoría
    void filtrar() {
        QString texto = buscar->text().trimmed();
        QString categoria = cmbCategoria->currentData().toString();

        for (int i = 0; i < listaProductos->count(); i++) {
            QListWidgetItem *item = listaProductos->item(i);
            QString nombre = item->data(Qt::UserRole + 3).toString();
            QString categoriaItem = item->data(Qt::UserRole + 4).toString();

            bool coincideTexto = texto.isEmpty() || nombre.contains(texto, Qt::CaseInsensitive);
            bool coincideCategoria = categoria.isEmpty() || categoriaItem == categoria;
            item->setHidden(!(coincideTexto && coincideCategoria));
        }
    }

    // ---------- Carrito ----------
    double calcularTotal() {
        double suma = 0;
        for (const auto &item : carrito)
            suma += item.precio * item.cantidad;
        return qRound64(suma * 100) / 100.0;
    }

    int cantidadEnCarrito(int id) {
        for (const auto &item : carrito) {
            if (item.id == id)
                return item.cantidad;
        }
        return 0;
    }

    // Un clic en un producto lo agrega al carrito
    void agregarAlCarrito(QListWidgetItem *item) {
        int id = item->data(Qt::UserRole).toInt();
        int stock = item->data(Qt::UserRole + 1).toInt();
        double precio = item->data(Qt::UserRole + 2).toDouble();
        QString nombre = item->data(Qt::UserRole + 3).toString();
        int cantidad = spCantidad->value();

        if (cantidadEnCarrito(id) + cantidad > stock) {
            aviso(this, "Existencia insuficiente de '" + nombre + "'. Disponible: " + QString::number(stock));
            return;
        }

        // Si ya estaba en el carrito, solo sumamos la cantidad
        bool encontrado = false;
        for (auto &it : carrito) {
            if (it.id == id) {
                it.cantidad += cantidad;
                encontrado = true;
                break;
            }
        }
        if (!encontrado) {
            ItemCarrito nuevo = {id, nombre, precio, cantidad};
            carrito.push_back(nuevo);
        }

        spCantidad->setValue(1);
        mostrarCarrito();
        tabla->scrollToBottom();
    }

    // Botones + y - de la fila seleccionada
    void cambiarCantidad(int cambio) {
        int fila = tabla->currentRow();
        if (fila < 0 || fila >= carrito.size()) {
            aviso(this, "Selecciona una fila del carrito.");
            return;
        }

        int nueva = carrito[fila].cantidad + cambio;
        int disponible = stockPorId.value(carrito[fila].id, 0);

        if (nueva <= 0) {
            carrito.removeAt(fila);
        } else if (nueva > disponible) {
            aviso(this, "Existencia insuficiente. Disponible: " + QString::number(disponible));
            return;
        } else {
            carrito[fila].cantidad = nueva;
        }

        mostrarCarrito();
        if (tabla->rowCount() > 0)
            tabla->selectRow(qMin(fila, tabla->rowCount() - 1));
    }

    void quitarDelCarrito() {
        int fila = tabla->currentRow();
        if (fila < 0) {
            aviso(this, "Selecciona una fila del carrito.");
            return;
        }
        carrito.removeAt(fila);
        mostrarCarrito();
    }

    void vaciarCarrito() {
        carrito.clear();
        mostrarCarrito();
    }

    // Dibuja el carrito en la tabla y actualiza el total
    void mostrarCarrito() {
        QVector<QStringList> filas;
        for (const auto &item : carrito) {
            QStringList fila;
            fila << item.nombre
                 << dinero(item.precio)
                 << QString::number(item.cantidad)
                 << dinero(item.precio * item.cantidad);
            filas.push_back(fila);
        }
        llenarTabla(tabla, filas);
        lblTotal->setText("Total: " + dinero(calcularTotal()));
        actualizarCambio();
    }

    void actualizarCambio() {
        bool efectivo = (cmbPago->currentText() == "Efectivo");
        spRecibido->setEnabled(efectivo);
        if (efectivo) {
            double cambio = spRecibido->value() - calcularTotal();
            if (cambio < 0)
                cambio = 0;
            lblCambio->setText("Cambio: " + dinero(cambio));
        } else {
            lblCambio->setText("");
        }
    }

    // ---------- Cobro ----------
    void cobrar() {
        if (carrito.isEmpty()) {
            aviso(this, "El carrito está vacío.");
            return;
        }

        double total = calcularTotal();
        QString metodo = cmbPago->currentText();
        bool efectivo = (metodo == "Efectivo");

        if (efectivo && spRecibido->value() + 0.001 < total) {
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
        spRecibido->setValue(0);
        refrescar();
        mostrarTicket(this, ticket, "ticket_" + QString::number(folio));
    }

    // Guarda la venta en la base de datos.
    // Si algo falla se cancela todo (ROLLBACK). Devuelve "" si todo salió bien.
    QString guardarVenta(const QString &metodo, double total, qulonglong &folio) {
        int clienteId = cmbCliente->currentData().toInt();
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
        folio = mysql_insert_id(conexion);

        // 2) Descontar existencia y guardar el detalle de cada producto
        for (int i = 0; error.isEmpty() && i < carrito.size(); i++) {
            ItemCarrito item = carrito[i];

            QString sqlStock = "UPDATE productos SET existencia = existencia - " + QString::number(item.cantidad) +
                               " WHERE id = " + QString::number(item.id) +
                               " AND existencia >= " + QString::number(item.cantidad);
            if (!ejecutar(sqlStock)) {
                error = errorBD();
            } else if (mysql_affected_rows(conexion) != 1) {
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

    QString armarTicket(qulonglong folio, const QString &metodo, double total, bool efectivo) {
        QString t;
        t += "      " + NOMBRE_NEGOCIO.toUpper() + "\n";
        t += "Folio:   " + QString::number(folio) + "\n";
        t += "Fecha:   " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") + "\n";
        t += "Cajero:  " + sesionUsuario + "\n";
        t += "Cliente: " + cmbCliente->currentText() + "\n";
        t += QString("-").repeated(40) + "\n";
        for (const auto &item : carrito) {
            t += item.nombre.leftJustified(24, ' ', true);
            t += QString("x%1").arg(item.cantidad).rightJustified(5);
            t += dinero(item.precio * item.cantidad).rightJustified(11) + "\n";
        }
        t += QString("-").repeated(40) + "\n";
        t += QString("TOTAL:").leftJustified(29) + dinero(total).rightJustified(11) + "\n";
        t += "Pago: " + metodo + "\n";
        if (efectivo) {
            t += "Recibido: " + dinero(spRecibido->value()) + "\n";
            t += "Cambio:   " + dinero(spRecibido->value() - total) + "\n";
        }
        t += "\n      ¡Gracias por su compra!\n";
        return t;
    }
};

// =============================================================================
// 9. PÁGINA DE INVENTARIO (el vendedor solo puede consultar)
// =============================================================================
class PaginaInventario : public Pagina {
    QTableWidget *tabla;
    QLineEdit *buscar;
    QLineEdit *entNombre;
    QLineEdit *entCategoria;
    QSpinBox *entStock;
    QDoubleSpinBox *entPrecio;
    QComboBox *cmbProv;
    QGroupBox *formulario;
    QLabel *lblPreview;
    QString imagenActual;   // nombre del archivo de imagen del producto
    int idSel = 0;          // id del producto seleccionado (0 = ninguno)

public:
    PaginaInventario(QWidget *padre = nullptr) : Pagina(padre) {
        crearInterfaz();
        conectarEventos();
    }

    void refrescar() override {
        // Llenar la lista de proveedores
        cmbProv->clear();
        cmbProv->addItem("(Sin proveedor)", 0);
        auto proveedores = consultar("SELECT id, nombre_empresa FROM proveedores ORDER BY nombre_empresa");
        for (const auto &p : proveedores)
            cmbProv->addItem(p[1], p[0].toInt());

        cargar();
    }

private:
    void crearInterfaz() {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(crearTitulo("Inventario"));

        buscar = new QLineEdit;
        buscar->setPlaceholderText("Buscar por nombre o categoría...");
        layout->addWidget(buscar);

        tabla = crearTabla({"ID", "Artículo", "Categoría", "Existencia", "Precio", "Proveedor", "ProvID", "Imagen"});
        tabla->setColumnHidden(6, true);   // columnas auxiliares ocultas
        tabla->setColumnHidden(7, true);
        layout->addWidget(tabla, 1);

        // Formulario
        formulario = new QGroupBox("Datos del producto");
        QFormLayout *form = new QFormLayout(formulario);

        entNombre = new QLineEdit;
        entCategoria = new QLineEdit;
        entCategoria->setPlaceholderText("ej: Bebidas, Snacks, Desinfectantes");
        entStock = new QSpinBox;
        entStock->setRange(0, 1000000);
        entPrecio = new QDoubleSpinBox;
        entPrecio->setRange(0, 1000000);
        entPrecio->setDecimals(2);
        entPrecio->setPrefix("$");
        cmbProv = new QComboBox;

        form->addRow("Nombre:", entNombre);
        form->addRow("Categoría:", entCategoria);
        form->addRow("Existencia:", entStock);
        form->addRow("Precio:", entPrecio);
        form->addRow("Proveedor:", cmbProv);

        // Imagen
        lblPreview = new QLabel;
        lblPreview->setObjectName("preview");
        lblPreview->setFixedSize(76, 76);
        lblPreview->setAlignment(Qt::AlignCenter);
        QPushButton *btnImagen = new QPushButton("Elegir imagen...");
        QPushButton *btnQuitarImagen = new QPushButton("Quitar imagen");
        QHBoxLayout *filaImagen = new QHBoxLayout;
        filaImagen->addWidget(lblPreview);
        filaImagen->addWidget(btnImagen);
        filaImagen->addWidget(btnQuitarImagen);
        filaImagen->addStretch();
        form->addRow("Imagen:", filaImagen);

        // Botones
        QPushButton *btnCrear = crearBoton("Añadir", "exito");
        QPushButton *btnMod = crearBoton("Modificar", "aviso");
        QPushButton *btnDel = crearBoton("Eliminar", "peligro");
        QPushButton *btnLimpiar = new QPushButton("Limpiar");
        QHBoxLayout *botones = new QHBoxLayout;
        botones->addWidget(btnCrear);
        botones->addWidget(btnMod);
        botones->addWidget(btnDel);
        botones->addWidget(btnLimpiar);
        form->addRow(botones);
        layout->addWidget(formulario);

        // El vendedor no puede modificar el inventario
        formulario->setVisible(esAdmin());

        connect(btnCrear, &QPushButton::clicked, this, [this]() { guardar(true); });
        connect(btnMod, &QPushButton::clicked, this, [this]() { guardar(false); });
        connect(btnDel, &QPushButton::clicked, this, &PaginaInventario::eliminar);
        connect(btnLimpiar, &QPushButton::clicked, this, &PaginaInventario::limpiar);
        connect(btnImagen, &QPushButton::clicked, this, &PaginaInventario::elegirImagen);
        connect(btnQuitarImagen, &QPushButton::clicked, this, &PaginaInventario::quitarImagen);
    }

    void conectarEventos() {
        connect(buscar, &QLineEdit::textChanged, this, &PaginaInventario::cargar);
        connect(tabla, &QTableWidget::itemSelectionChanged, this, &PaginaInventario::seleccion);
        connect(entNombre, &QLineEdit::textChanged, this, &PaginaInventario::actualizarPreview);
        connect(entCategoria, &QLineEdit::textChanged, this, &PaginaInventario::actualizarPreview);
        actualizarPreview();
    }

    // Llena la tabla con los productos (aplicando la búsqueda)
    void cargar() {
        QString b = esc(buscar->text().trimmed());
        auto filas = consultar(
            "SELECT p.id, p.nombre_articulo, COALESCE(p.categoria,''), p.existencia, p.precio, "
            "COALESCE(pr.nombre_empresa,'—'), COALESCE(p.proveedor_id,0), COALESCE(p.imagen,'') "
            "FROM productos p LEFT JOIN proveedores pr ON pr.id = p.proveedor_id "
            "WHERE p.nombre_articulo LIKE '%" + b + "%' OR p.categoria LIKE '%" + b + "%' "
                                                    "ORDER BY p.nombre_articulo");

        for (auto &fila : filas)
            fila[4] = dinero(fila[4].toDouble());
        llenarTabla(tabla, filas);

        // Pintar de rojo claro los productos con existencia baja
        for (int r = 0; r < tabla->rowCount(); r++) {
            if (tabla->item(r, 3)->text().toInt() <= STOCK_BAJO) {
                for (int c = 0; c < tabla->columnCount(); c++) {
                    tabla->item(r, c)->setBackground(QColor(255, 220, 220));
                    tabla->item(r, c)->setForeground(Qt::black);
                }
            }
        }
    }

    // Al elegir una fila, se copian sus datos al formulario
    void seleccion() {
        int fila = tabla->currentRow();
        if (fila < 0 || tabla->item(fila, 0) == nullptr)
            return;

        idSel = tabla->item(fila, 0)->text().toInt();
        entNombre->setText(tabla->item(fila, 1)->text());
        entCategoria->setText(tabla->item(fila, 2)->text());
        entStock->setValue(tabla->item(fila, 3)->text().toInt());
        entPrecio->setValue(tabla->item(fila, 4)->text().remove('$').toDouble());

        int pos = cmbProv->findData(tabla->item(fila, 6)->text().toInt());
        if (pos >= 0)
            cmbProv->setCurrentIndex(pos);
        else
            cmbProv->setCurrentIndex(0);

        imagenActual = tabla->item(fila, 7)->text();
        actualizarPreview();
    }

    void actualizarPreview() {
        QPixmap imagen = imagenProducto(imagenActual, entNombre->text(), entCategoria->text(), 128);
        lblPreview->setPixmap(imagen.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // Copia la imagen elegida (reducida) a la carpeta de la aplicación.
    // Se asocia al producto cuando se pulsa Añadir o Modificar.
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

    void quitarImagen() {
        imagenActual.clear();
        actualizarPreview();
    }

    void limpiar() {
        idSel = 0;
        entNombre->clear();
        entCategoria->clear();
        entStock->setValue(0);
        entPrecio->setValue(0);
        cmbProv->setCurrentIndex(0);
        imagenActual.clear();
        actualizarPreview();
        tabla->clearSelection();
    }

    // nuevo = true -> INSERT, nuevo = false -> UPDATE
    void guardar(bool nuevo) {
        QString nombre = entNombre->text().trimmed();
        if (nombre.isEmpty()) {
            aviso(this, "El nombre es obligatorio.");
            return;
        }
        if (entPrecio->value() <= 0) {
            aviso(this, "El precio debe ser mayor a cero.");
            return;
        }
        if (!nuevo && idSel == 0) {
            aviso(this, "Selecciona un producto de la tabla para modificarlo.");
            return;
        }

        int provId = cmbProv->currentData().toInt();
        QString proveedor = "NULL";
        if (provId != 0)
            proveedor = QString::number(provId);

        QString sql;
        if (nuevo) {
            sql = "INSERT INTO productos (nombre_articulo, categoria, existencia, precio, proveedor_id, imagen) VALUES (" +
                  sqlTxt(nombre) + ", " + sqlTxt(entCategoria->text()) + ", " + QString::number(entStock->value()) + ", " +
                  QString::number(entPrecio->value(), 'f', 2) + ", " + proveedor + ", " + sqlTxt(imagenActual) + ")";
        } else {
            sql = "UPDATE productos SET nombre_articulo=" + sqlTxt(nombre) +
                  ", categoria=" + sqlTxt(entCategoria->text()) +
                  ", existencia=" + QString::number(entStock->value()) +
                  ", precio=" + QString::number(entPrecio->value(), 'f', 2) +
                  ", proveedor_id=" + proveedor +
                  ", imagen=" + sqlTxt(imagenActual) +
                  " WHERE id=" + QString::number(idSel);
        }

        if (ejecutar(sql)) {
            if (nuevo)
                info(this, "'" + nombre + "' agregado correctamente.");
            else
                info(this, "Producto modificado.");
            limpiar();
            cargar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un producto de la tabla.");
            return;
        }
        if (!confirmar(this, "¿Eliminar '" + entNombre->text() + "'?"))
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
// 10. PÁGINA DE CLIENTES
// =============================================================================
class PaginaClientes : public Pagina {
    QTableWidget *tabla;
    QLineEdit *buscar;
    QLineEdit *entNombre;
    QLineEdit *entTelefono;
    QLineEdit *entEmail;
    QLineEdit *entDireccion;
    int idSel = 0;

public:
    PaginaClientes(QWidget *padre = nullptr) : Pagina(padre) {
        crearInterfaz();
    }

    void refrescar() override {
        QString b = esc(buscar->text().trimmed());
        auto filas = consultar(
            "SELECT c.id, c.nombre, COALESCE(c.telefono,''), COALESCE(c.email,''), "
            "COALESCE(c.direccion,''), DATE(c.fecha_registro), COALESCE(SUM(v.total),0) "
            "FROM clientes c LEFT JOIN ventas v ON v.cliente_id = c.id "
            "WHERE c.nombre LIKE '%" + b + "%' OR c.telefono LIKE '%" + b + "%' OR c.email LIKE '%" + b + "%' "
                                                                                 "GROUP BY c.id ORDER BY c.nombre");

        for (auto &fila : filas)
            fila[6] = dinero(fila[6].toDouble());
        llenarTabla(tabla, filas);
    }

private:
    void crearInterfaz() {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(crearTitulo("Clientes"));

        buscar = new QLineEdit;
        buscar->setPlaceholderText("Buscar por nombre, teléfono o correo...");
        layout->addWidget(buscar);

        tabla = crearTabla({"ID", "Nombre", "Teléfono", "Email", "Dirección", "Registro", "Total comprado"});
        layout->addWidget(tabla, 1);

        QGroupBox *formulario = new QGroupBox("Datos del cliente");
        QFormLayout *form = new QFormLayout(formulario);
        entNombre = new QLineEdit;
        entTelefono = new QLineEdit;
        entEmail = new QLineEdit;
        entDireccion = new QLineEdit;
        form->addRow("Nombre:", entNombre);
        form->addRow("Teléfono:", entTelefono);
        form->addRow("Email:", entEmail);
        form->addRow("Dirección:", entDireccion);

        QPushButton *btnCrear = crearBoton("Añadir", "exito");
        QPushButton *btnMod = crearBoton("Modificar", "aviso");
        QPushButton *btnDel = crearBoton("Eliminar", "peligro");
        QPushButton *btnLimpiar = new QPushButton("Limpiar");
        QHBoxLayout *botones = new QHBoxLayout;
        botones->addWidget(btnCrear);
        botones->addWidget(btnMod);
        botones->addWidget(btnDel);
        botones->addWidget(btnLimpiar);
        form->addRow(botones);
        layout->addWidget(formulario);

        // Solo el administrador puede eliminar clientes
        btnDel->setVisible(esAdmin());

        connect(buscar, &QLineEdit::textChanged, this, &PaginaClientes::refrescar);
        connect(tabla, &QTableWidget::itemSelectionChanged, this, &PaginaClientes::seleccion);
        connect(btnCrear, &QPushButton::clicked, this, [this]() { guardar(true); });
        connect(btnMod, &QPushButton::clicked, this, [this]() { guardar(false); });
        connect(btnDel, &QPushButton::clicked, this, &PaginaClientes::eliminar);
        connect(btnLimpiar, &QPushButton::clicked, this, &PaginaClientes::limpiar);
    }

    void seleccion() {
        int fila = tabla->currentRow();
        if (fila < 0 || tabla->item(fila, 0) == nullptr)
            return;

        idSel = tabla->item(fila, 0)->text().toInt();
        entNombre->setText(tabla->item(fila, 1)->text());
        entTelefono->setText(tabla->item(fila, 2)->text());
        entEmail->setText(tabla->item(fila, 3)->text());
        entDireccion->setText(tabla->item(fila, 4)->text());
    }

    void limpiar() {
        idSel = 0;
        entNombre->clear();
        entTelefono->clear();
        entEmail->clear();
        entDireccion->clear();
        tabla->clearSelection();
    }

    void guardar(bool nuevo) {
        QString nombre = entNombre->text().trimmed();
        if (nombre.isEmpty()) {
            aviso(this, "El nombre del cliente es obligatorio.");
            return;
        }
        if (!nuevo && idSel == 0) {
            aviso(this, "Selecciona un cliente de la tabla para modificarlo.");
            return;
        }

        QString sql;
        if (nuevo) {
            sql = "INSERT INTO clientes (nombre, telefono, email, direccion) VALUES (" +
                  sqlTxt(nombre) + ", " + sqlTxt(entTelefono->text()) + ", " +
                  sqlTxt(entEmail->text()) + ", " + sqlTxt(entDireccion->text()) + ")";
        } else {
            sql = "UPDATE clientes SET nombre=" + sqlTxt(nombre) +
                  ", telefono=" + sqlTxt(entTelefono->text()) +
                  ", email=" + sqlTxt(entEmail->text()) +
                  ", direccion=" + sqlTxt(entDireccion->text()) +
                  " WHERE id=" + QString::number(idSel);
        }

        if (ejecutar(sql)) {
            if (nuevo)
                info(this, "Cliente registrado.");
            else
                info(this, "Cliente actualizado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo guardar: " + errorBD());
        }
    }

    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un cliente de la tabla.");
            return;
        }
        QString pregunta = "¿Eliminar al cliente '" + entNombre->text() +
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
// 11. PÁGINA DE USUARIOS (solo administrador)
// =============================================================================
class PaginaUsuarios : public Pagina {
    QTableWidget *tabla;
    QLineEdit *entUsuario;
    QLineEdit *entEmail;
    QLineEdit *entPassword;
    QComboBox *cmbRol;
    QCheckBox *chkActivo;
    int idSel = 0;

public:
    PaginaUsuarios(QWidget *padre = nullptr) : Pagina(padre) {
        crearInterfaz();
    }

    void refrescar() override {
        auto filas = consultar("SELECT id, usuario, COALESCE(email,''), rol, IF(activo=1,'Sí','No') "
                               "FROM usuarios ORDER BY usuario");
        llenarTabla(tabla, filas);
    }

private:
    void crearInterfaz() {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(crearTitulo("Usuarios del sistema"));

        tabla = crearTabla({"ID", "Usuario", "Email", "Rol", "Activo"});
        layout->addWidget(tabla, 1);

        QGroupBox *formulario = new QGroupBox("Datos del usuario");
        QFormLayout *form = new QFormLayout(formulario);

        entUsuario = new QLineEdit;
        entEmail = new QLineEdit;
        entPassword = new QLineEdit;
        entPassword->setEchoMode(QLineEdit::Password);
        entPassword->setPlaceholderText("Mínimo 6 caracteres (al modificar, vacío = no cambiar)");
        cmbRol = new QComboBox;
        cmbRol->addItems({"vendedor", "administrador"});
        chkActivo = new QCheckBox("Usuario activo");
        chkActivo->setChecked(true);

        form->addRow("Usuario:", entUsuario);
        form->addRow("Email:", entEmail);
        form->addRow("Contraseña:", entPassword);
        form->addRow("Rol:", cmbRol);
        form->addRow("", chkActivo);

        QPushButton *btnCrear = crearBoton("Crear usuario", "exito");
        QPushButton *btnMod = crearBoton("Modificar", "aviso");
        QPushButton *btnDel = crearBoton("Eliminar", "peligro");
        QPushButton *btnLimpiar = new QPushButton("Limpiar");
        QHBoxLayout *botones = new QHBoxLayout;
        botones->addWidget(btnCrear);
        botones->addWidget(btnMod);
        botones->addWidget(btnDel);
        botones->addWidget(btnLimpiar);
        form->addRow(botones);
        layout->addWidget(formulario);

        connect(tabla, &QTableWidget::itemSelectionChanged, this, &PaginaUsuarios::seleccion);
        connect(btnCrear, &QPushButton::clicked, this, &PaginaUsuarios::crear);
        connect(btnMod, &QPushButton::clicked, this, &PaginaUsuarios::modificar);
        connect(btnDel, &QPushButton::clicked, this, &PaginaUsuarios::eliminar);
        connect(btnLimpiar, &QPushButton::clicked, this, &PaginaUsuarios::limpiar);
    }

    void seleccion() {
        int fila = tabla->currentRow();
        if (fila < 0 || tabla->item(fila, 0) == nullptr)
            return;

        idSel = tabla->item(fila, 0)->text().toInt();
        entUsuario->setText(tabla->item(fila, 1)->text());
        entEmail->setText(tabla->item(fila, 2)->text());
        entPassword->clear();
        cmbRol->setCurrentText(tabla->item(fila, 3)->text());
        chkActivo->setChecked(tabla->item(fila, 4)->text() == "Sí");
    }

    void limpiar() {
        idSel = 0;
        entUsuario->clear();
        entEmail->clear();
        entPassword->clear();
        cmbRol->setCurrentIndex(0);
        chkActivo->setChecked(true);
        tabla->clearSelection();
    }

    void crear() {
        QString usuario = entUsuario->text().trimmed();
        QString password = entPassword->text();

        if (usuario.isEmpty()) {
            aviso(this, "El nombre de usuario es obligatorio.");
            return;
        }
        if (password.size() < 6) {
            aviso(this, "La contraseña debe tener al menos 6 caracteres.");
            return;
        }

        QString salt = generarSalt();
        QString activo = chkActivo->isChecked() ? "1" : "0";
        QString sql = "INSERT INTO usuarios (usuario, password, salt, email, rol, activo) VALUES (" +
                      sqlTxt(usuario) + ", '" + hashPassword(salt, password) + "', '" + salt + "', " +
                      sqlTxt(entEmail->text()) + ", '" + cmbRol->currentText() + "', " + activo + ")";

        if (ejecutar(sql)) {
            info(this, "Usuario '" + usuario + "' creado.");
            limpiar();
            refrescar();
        } else {
            falla(this, "No se pudo crear el usuario (¿ya existe ese nombre?):\n" + errorBD());
        }
    }

    void modificar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un usuario de la tabla.");
            return;
        }
        QString usuario = entUsuario->text().trimmed();
        if (usuario.isEmpty()) {
            aviso(this, "El nombre de usuario es obligatorio.");
            return;
        }
        // Para no quedarse sin acceso, no puede quitarse su propio permiso
        if (idSel == sesionId && (!chkActivo->isChecked() || cmbRol->currentText() != "administrador")) {
            aviso(this, "No puedes desactivarte ni quitarte el rol de administrador a ti mismo.");
            return;
        }
        QString password = entPassword->text();
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

        QString activo = chkActivo->isChecked() ? "1" : "0";
        QString sql = "UPDATE usuarios SET usuario=" + sqlTxt(usuario) +
                      ", email=" + sqlTxt(entEmail->text()) +
                      ", rol='" + cmbRol->currentText() + "'" +
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

    void eliminar() {
        if (idSel == 0) {
            aviso(this, "Selecciona un usuario de la tabla.");
            return;
        }
        if (idSel == sesionId) {
            aviso(this, "No puedes eliminar tu propio usuario.");
            return;
        }
        if (!confirmar(this, "¿Eliminar al usuario '" + entUsuario->text() + "'?"))
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
// 12. PÁGINA DE REPORTES (se pueden exportar a .txt)
// El vendedor solo ve sus propias ventas; el administrador ve todo.
// =============================================================================
class PaginaReportes : public Pagina {
    QComboBox *cmbTipo;
    QDateEdit *dDesde;
    QDateEdit *dHasta;
    QTextEdit *vista;
    QString nombreArchivo = "reporte";

public:
    PaginaReportes(QWidget *padre = nullptr) : Pagina(padre) {
        crearInterfaz();
    }

    void refrescar() override {
        // Los reportes se generan solo al pulsar "Generar"
    }

private:
    void crearInterfaz() {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(crearTitulo("Reportes"));

        // Tipo de reporte
        cmbTipo = new QComboBox;
        cmbTipo->addItem("Ventas del periodo", "ventas");
        cmbTipo->addItem("Productos vendidos", "vendidos");
        cmbTipo->addItem("Corte de caja (por método de pago)", "corte");
        if (esAdmin())
            cmbTipo->addItem("Ventas por vendedor", "vendedores");
        cmbTipo->addItem("Inventario actual", "inventario");
        cmbTipo->addItem("Productos con existencia baja", "stock");
        cmbTipo->addItem("Clientes y total comprado", "clientes");

        // Fechas (por defecto: del día 1 del mes a hoy)
        QDate hoy = QDate::currentDate();
        dDesde = new QDateEdit(QDate(hoy.year(), hoy.month(), 1));
        dHasta = new QDateEdit(hoy);
        dDesde->setCalendarPopup(true);
        dHasta->setCalendarPopup(true);
        dDesde->setDisplayFormat("yyyy-MM-dd");
        dHasta->setDisplayFormat("yyyy-MM-dd");

        QHBoxLayout *fila = new QHBoxLayout;
        fila->addWidget(new QLabel("Reporte:"));
        fila->addWidget(cmbTipo, 1);
        fila->addWidget(new QLabel("Desde:"));
        fila->addWidget(dDesde);
        fila->addWidget(new QLabel("Hasta:"));
        fila->addWidget(dHasta);
        layout->addLayout(fila);

        // Botones
        QPushButton *btnGenerar = new QPushButton("Generar");
        QPushButton *btnExportar = new QPushButton("Exportar a .txt");
        QHBoxLayout *filaBotones = new QHBoxLayout;
        filaBotones->addWidget(btnGenerar);
        filaBotones->addWidget(btnExportar);
        filaBotones->addStretch();
        layout->addLayout(filaBotones);

        // Vista previa del reporte
        vista = new QTextEdit;
        vista->setReadOnly(true);
        vista->setLineWrapMode(QTextEdit::NoWrap);
        vista->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(vista, 1);

        connect(btnGenerar, &QPushButton::clicked, this, &PaginaReportes::generar);
        connect(btnExportar, &QPushButton::clicked, this, &PaginaReportes::exportar);
        connect(cmbTipo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { activarFechas(); });
    }

    // Algunos reportes no usan fechas
    void activarFechas() {
        QString clave = cmbTipo->currentData().toString();
        bool usaFechas = !(clave == "inventario" || clave == "stock" || clave == "clientes");
        dDesde->setEnabled(usaFechas);
        dHasta->setEnabled(usaFechas);
    }

    void exportar() {
        if (vista->toPlainText().isEmpty()) {
            aviso(this, "Primero genera un reporte.");
            return;
        }
        exportarTxt(this, nombreArchivo, vista->toPlainText());
    }

    // Arma el reporte elegido y lo muestra en pantalla
    void generar() {
        QString clave = cmbTipo->currentData().toString();
        bool usaFechas = dDesde->isEnabled();

        if (usaFechas && dDesde->date() > dHasta->date()) {
            aviso(this, "La fecha inicial no puede ser posterior a la final.");
            return;
        }

        QString desde = dDesde->date().toString("yyyy-MM-dd");
        QString hasta = dHasta->date().toString("yyyy-MM-dd");
        QString rango = "DATE(v.fecha) BETWEEN '" + desde + "' AND '" + hasta + "'";

        // El vendedor solo ve sus ventas
        QString soloMias = "";
        if (!esAdmin())
            soloMias = " AND v.usuario_id = " + QString::number(sesionId);

        nombreArchivo = "reporte_" + clave;

        QString cuerpo;
        if (clave == "ventas")           cuerpo = reporteVentas(rango, soloMias);
        else if (clave == "vendidos")    cuerpo = reporteVendidos(rango, soloMias);
        else if (clave == "corte")       cuerpo = reporteCorte(rango, soloMias);
        else if (clave == "vendedores")  cuerpo = reporteVendedores(rango);
        else if (clave == "inventario")  cuerpo = reporteInventario();
        else if (clave == "stock")       cuerpo = reporteStockBajo();
        else if (clave == "clientes")    cuerpo = reporteClientes();

        // Encabezado del reporte
        QString linea = QString("=").repeated(60);
        QString txt = linea + "\n";
        txt += NOMBRE_NEGOCIO + " — " + cmbTipo->currentText().toUpper() + "\n";
        txt += "Generado: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") +
               "   Por: " + sesionUsuario + " (" + sesionRol + ")\n";
        if (usaFechas)
            txt += "Periodo:  " + desde + " al " + hasta + "\n";
        if (usaFechas && !esAdmin())
            txt += "Solo incluye tus propias ventas.\n";
        txt += linea + "\n\n";
        txt += cuerpo;

        vista->setPlainText(txt);
    }

    // ---------- Un método por cada tipo de reporte ----------
    QString reporteVentas(const QString &rango, const QString &soloMias) {
        auto filas = consultar(
            "SELECT v.id, v.fecha, COALESCE(c.nombre,'Público general'), u.usuario, v.metodo_pago, v.total "
            "FROM ventas v JOIN usuarios u ON u.id = v.usuario_id "
            "LEFT JOIN clientes c ON c.id = v.cliente_id "
            "WHERE " + rango + soloMias + " ORDER BY v.fecha");

        double suma = formatearColumnaDinero(filas, 5);
        QString texto = tablaTxt({"Folio", "Fecha", "Cliente", "Vendedor", "Pago", "Total"}, filas,
                                 {6, 19, 22, 12, 13, 12}, "LLLLLR");
        texto += "\nVentas: " + QString::number(filas.size()) + "     Total vendido: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteVendidos(const QString &rango, const QString &soloMias) {
        auto filas = consultar(
            "SELECT d.nombre_articulo, SUM(d.cantidad), SUM(d.subtotal) FROM detalle_venta d "
            "JOIN ventas v ON v.id = d.venta_id WHERE " + rango + soloMias +
            " GROUP BY d.nombre_articulo ORDER BY SUM(d.cantidad) DESC");

        double suma = formatearColumnaDinero(filas, 2);
        QString texto = tablaTxt({"Producto", "Unidades", "Importe"}, filas, {34, 10, 14}, "LRR");
        texto += "\nImporte total: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteCorte(const QString &rango, const QString &soloMias) {
        auto filas = consultar(
            "SELECT v.metodo_pago, COUNT(*), SUM(v.total) FROM ventas v WHERE " + rango + soloMias +
            " GROUP BY v.metodo_pago ORDER BY v.metodo_pago");

        double suma = formatearColumnaDinero(filas, 2);
        QString texto = tablaTxt({"Método de pago", "Ventas", "Total"}, filas, {20, 8, 14}, "LRR");
        texto += "\nTotal en caja: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteVendedores(const QString &rango) {
        auto filas = consultar(
            "SELECT u.usuario, COUNT(*), SUM(v.total) FROM ventas v "
            "JOIN usuarios u ON u.id = v.usuario_id WHERE " + rango +
            " GROUP BY u.id ORDER BY SUM(v.total) DESC");

        double suma = formatearColumnaDinero(filas, 2);
        QString texto = tablaTxt({"Vendedor", "Ventas", "Total"}, filas, {24, 8, 14}, "LRR");
        texto += "\nTotal general: " + dinero(suma) + "\n";
        return texto;
    }

    QString reporteInventario() {
        auto filas = consultar(
            "SELECT id, nombre_articulo, COALESCE(categoria,''), existencia, precio, existencia*precio "
            "FROM productos ORDER BY categoria, nombre_articulo");

        double valor = 0;
        for (auto &fila : filas) {
            valor += fila[5].toDouble();
            fila[4] = dinero(fila[4].toDouble());
            fila[5] = dinero(fila[5].toDouble());
        }
        QString texto = tablaTxt({"ID", "Artículo", "Categoría", "Exist.", "Precio", "Valor"}, filas,
                                 {4, 26, 16, 7, 10, 12}, "RLLRRR");
        texto += "\nProductos: " + QString::number(filas.size()) + "     Valor del inventario: " + dinero(valor) + "\n";
        return texto;
    }

    QString reporteStockBajo() {
        auto filas = consultar(
            "SELECT p.id, p.nombre_articulo, COALESCE(p.categoria,''), p.existencia, "
            "COALESCE(pr.nombre_empresa,'—') FROM productos p "
            "LEFT JOIN proveedores pr ON pr.id = p.proveedor_id "
            "WHERE p.existencia <= " + QString::number(STOCK_BAJO) +
            " ORDER BY p.existencia, p.nombre_articulo");

        QString texto = "Productos con existencia menor o igual a " + QString::number(STOCK_BAJO) + "\n\n";
        texto += tablaTxt({"ID", "Artículo", "Categoría", "Exist.", "Proveedor"}, filas,
                          {4, 26, 16, 7, 24}, "RLLRL");
        texto += "\nProductos por reabastecer: " + QString::number(filas.size()) + "\n";
        return texto;
    }

    QString reporteClientes() {
        auto filas = consultar(
            "SELECT c.id, c.nombre, COALESCE(c.telefono,''), COALESCE(c.email,''), COUNT(v.id), "
            "COALESCE(SUM(v.total),0) FROM clientes c LEFT JOIN ventas v ON v.cliente_id = c.id "
            "GROUP BY c.id ORDER BY SUM(v.total) DESC, c.nombre");

        formatearColumnaDinero(filas, 5);
        return tablaTxt({"ID", "Nombre", "Teléfono", "Email", "Compras", "Total"}, filas,
                        {4, 24, 12, 26, 8, 12}, "RLLLRR");
    }
};

// =============================================================================
// 13. VENTANA PRINCIPAL (menú lateral + páginas según el rol)
// =============================================================================
class VentanaPrincipal : public QWidget {
    QListWidget *menu;
    QStackedWidget *paginas;
    QWidget *ventanaLogin;   // para volver al login al cerrar sesión

public:
    VentanaPrincipal(QWidget *login) {
        ventanaLogin = login;
        setWindowTitle(NOMBRE_NEGOCIO + " - Punto de Venta");
        resize(1150, 720);

        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(14);

        // --- Panel lateral ---
        QWidget *panel = new QWidget;
        panel->setObjectName("sidebar");
        panel->setFixedWidth(220);
        QVBoxLayout *layoutPanel = new QVBoxLayout(panel);
        layoutPanel->setContentsMargins(12, 14, 12, 14);

        QLabel *lblNegocio = new QLabel(NOMBRE_NEGOCIO);
        lblNegocio->setObjectName("negocio");
        lblNegocio->setWordWrap(true);
        layoutPanel->addWidget(lblNegocio);

        menu = new QListWidget;
        menu->setObjectName("menu");
        layoutPanel->addWidget(menu, 1);

        QLabel *lblSesion = new QLabel("Usuario: " + sesionUsuario + "\nRol: " + sesionRol);
        lblSesion->setObjectName("sesion");
        layoutPanel->addWidget(lblSesion);

        QPushButton *btnSalir = new QPushButton("Cerrar sesión");
        layoutPanel->addWidget(btnSalir);
        layout->addWidget(panel);

        // --- Zona de páginas ---
        paginas = new QStackedWidget;
        layout->addWidget(paginas, 1);

        agregarPagina("Ventas", new PaginaVentas);
        agregarPagina("Inventario", new PaginaInventario);
        agregarPagina("Clientes", new PaginaClientes);
        if (esAdmin())
            agregarPagina("Usuarios", new PaginaUsuarios);
        agregarPagina("Reportes", new PaginaReportes);

        connect(menu, &QListWidget::currentRowChanged, this, &VentanaPrincipal::cambiarPagina);
        connect(btnSalir, &QPushButton::clicked, this, &VentanaPrincipal::cerrarSesion);

        menu->setCurrentRow(0);
    }

private:
    void agregarPagina(const QString &titulo, Pagina *pagina) {
        menu->addItem(titulo);
        paginas->addWidget(pagina);
    }

    // Al cambiar de opción en el menú se muestra la página y se actualizan sus datos
    void cambiarPagina(int fila) {
        if (fila < 0)
            return;
        paginas->setCurrentIndex(fila);
        Pagina *pagina = static_cast<Pagina *>(paginas->currentWidget());
        pagina->refrescar();
    }

    void cerrarSesion() {
        if (!confirmar(this, "¿Cerrar la sesión?"))
            return;
        sesionId = 0;
        sesionUsuario = "";
        sesionRol = "";
        ventanaLogin->show();
        close();
    }
};

// =============================================================================
// 14. VENTANA DE LOGIN
// =============================================================================
class VentanaLogin : public QWidget {
    QLineEdit *entUsuario;
    QLineEdit *entPassword;

public:
    VentanaLogin() {
        setWindowTitle("Acceso - " + NOMBRE_NEGOCIO);
        setObjectName("login");
        resize(400, 400);

        QVBoxLayout *externo = new QVBoxLayout(this);
        externo->setContentsMargins(30, 30, 30, 30);
        QFrame *tarjeta = new QFrame;
        tarjeta->setObjectName("tarjeta");
        externo->addWidget(tarjeta);

        QVBoxLayout *layout = new QVBoxLayout(tarjeta);
        layout->setContentsMargins(28, 28, 28, 28);
        layout->setSpacing(12);

        QLabel *titulo = new QLabel("Iniciar Sesión");
        titulo->setObjectName("tituloLogin");
        layout->addWidget(titulo, 0, Qt::AlignCenter);

        QLabel *subtitulo = new QLabel(NOMBRE_NEGOCIO);
        subtitulo->setObjectName("subtituloLogin");
        layout->addWidget(subtitulo, 0, Qt::AlignCenter);
        layout->addStretch();

        entUsuario = new QLineEdit;
        entUsuario->setPlaceholderText("Usuario");
        layout->addWidget(entUsuario);

        entPassword = new QLineEdit;
        entPassword->setPlaceholderText("Contraseña");
        entPassword->setEchoMode(QLineEdit::Password);
        layout->addWidget(entPassword);

        QPushButton *btnIngresar = crearBoton("Ingresar", "primario");
        layout->addWidget(btnIngresar);
        layout->addStretch();

        connect(btnIngresar, &QPushButton::clicked, this, &VentanaLogin::intentarLogin);
        connect(entPassword, &QLineEdit::returnPressed, this, &VentanaLogin::intentarLogin);
        connect(entUsuario, &QLineEdit::returnPressed, this, [this]() { entPassword->setFocus(); });
    }

private:
    void intentarLogin() {
        QString usuario = entUsuario->text().trimmed();
        QString password = entPassword->text();

        if (usuario.isEmpty() || password.isEmpty()) {
            aviso(this, "Por favor llena todos los campos");
            return;
        }

        // Buscar el usuario en la base de datos
        auto filas = consultar("SELECT id, usuario, password, salt, rol, activo FROM usuarios "
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

        // Abrir la ventana principal y ocultar el login
        VentanaPrincipal *principal = new VentanaPrincipal(this);
        principal->setAttribute(Qt::WA_DeleteOnClose);
        entPassword->clear();
        entUsuario->setFocus();
        hide();
        principal->showMaximized();
    }
};

// =============================================================================
// 15. MAIN
// =============================================================================

// Lee un dato de conexión. Orden de prioridad:
//   1) variable de entorno (DB_HOST, DB_USER...)
//   2) archivo config.ini junto al ejecutable
//   3) valor por defecto
QString leerConfig(const QString &clave, const char *variableEntorno, const QString &porDefecto) {
    QString deEntorno = qEnvironmentVariable(variableEntorno);
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

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AbarroteraDonJose");
    QCoreApplication::setApplicationName("PuntoDeVenta");
    app.setStyle("Fusion");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);   // siempre tema claro
#endif
    cargarEstilo();

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
    mysql_close(conexion);
    return resultado;
}